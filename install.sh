#!/usr/bin/env bash
# BroadcastDecode installer for Raspberry Pi 5 (Raspberry Pi OS Lite, 64-bit).
#
#   git clone https://github.com/leonidis2013/BroadcastDecode
#   cd BroadcastDecode && sudo ./install.sh
#
# Re-run it any time to update (git pull first). Options:
#   --yes                  no questions, use defaults
#   --accept-ndi-license   accept the NDI SDK license without asking
#   --reconfigure          ask for source / audio output again
#   --source NAME          NDI source name (part of the name is fine)
#   --source-url IP[:PORT] direct address, for sources on another subnet
#   --audio-device DEV     ALSA device, e.g. hdmi:CARD=vc4hdmi0,DEV=0
#   --ndi-sdk DIR          use an already extracted "NDI SDK for Linux" folder
#   --ndi-tarball FILE     use a downloaded Install_NDI_SDK_*_Linux.tar.gz
#   --ndi-url URL          download the SDK tarball from this URL
#   --uninstall            remove the player and service (keeps /config)
set -euo pipefail

NDI_URL="https://downloads.ndi.tv/SDK/NDI_SDK_Linux/Install_NDI_SDK_v6_Linux.tar.gz"
NDI_PAGE="https://ndi.video/for-developers/ndi-sdk/download/"
SDK_HOME="/opt/ndi-sdk"
CONF="/config/config.json"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

YES=0; ACCEPT=0; RECONF=0
SRC=""; SRCURL=""; AUDIO=""; SDK_DIR=""; TARBALL=""

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mWarning: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31mError: %s\033[0m\n' "$*" >&2; exit 1; }

# ask "question" "default"  -> answer in $REPLY_VAL (reads from the keyboard even if piped)
ask() {
  local q="$1" def="${2:-}"
  if [[ $YES -eq 1 || ! -r /dev/tty ]]; then REPLY_VAL="$def"; return; fi
  read -r -p "$q ${def:+[$def] }" REPLY_VAL </dev/tty || true
  REPLY_VAL="${REPLY_VAL:-$def}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --yes) YES=1 ;;
    --accept-ndi-license) ACCEPT=1 ;;
    --reconfigure) RECONF=1 ;;
    --source) SRC="${2:?}"; shift ;;
    --source-url) SRCURL="${2:?}"; shift ;;
    --audio-device) AUDIO="${2:?}"; shift ;;
    --ndi-sdk) SDK_DIR="${2:?}"; shift ;;
    --ndi-tarball) TARBALL="${2:?}"; shift ;;
    --ndi-url) NDI_URL="${2:?}"; shift ;;
    --uninstall)
      [[ $EUID -eq 0 ]] || die "Run with sudo."
      systemctl disable --now broadcastdecode-player.service 2>/dev/null || true
      rm -f /etc/systemd/system/broadcastdecode-player.service /usr/local/bin/bdecode-player
      systemctl daemon-reload
      echo "Removed. Your settings in /config and the NDI SDK in $SDK_HOME were left in place."
      exit 0 ;;
    -h|--help) sed -n '2,19p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) die "Unknown option: $1 (try --help)" ;;
  esac
  shift
done

[[ $EUID -eq 0 ]] || die "Run with sudo:  sudo ./install.sh"

say "Checking this Pi"
[[ "$(uname -m)" == "aarch64" ]] || die "Needs 64-bit Raspberry Pi OS (found $(uname -m))."
MODEL="$(tr -d '\0' </proc/device-tree/model 2>/dev/null || echo unknown)"
echo "Board: $MODEL"
[[ "$MODEL" == *"Raspberry Pi 5"* ]] || warn "Built and tested for the Pi 5; this board may be too slow."

# ---------------------------------------------------------------- NDI SDK
sdk_ok() { [[ -f "$1/include/Processing.NDI.Lib.h" ]]; }

find_sdk() {
  local base p
  for base in "$SDK_HOME" "$PWD" /home/* /root /opt; do
    [[ -d "$base" ]] || continue
    sdk_ok "$base" && { echo "$base"; return 0; }
    while IFS= read -r p; do
      sdk_ok "$p" && { echo "$p"; return 0; }
    done < <(find "$base" -maxdepth 3 -type d -name 'NDI SDK for Linux' 2>/dev/null)
  done
  return 1
}

find_tarball() {
  local base
  for base in "$PWD" /home/* /root /tmp; do
    [[ -d "$base" ]] || continue
    find "$base" -maxdepth 3 -type f -name 'Install_NDI_SDK*Linux*.tar.gz' 2>/dev/null | head -n1
  done | head -n1
}

confirm_license() {
  [[ $ACCEPT -eq 1 ]] && return 0
  cat <<EOF

The NDI SDK is provided by Vizrt NDI AB under its own license (free for software use).
Read it at: $NDI_PAGE
EOF
  ask "Do you accept the NDI SDK license? (y/N)" "n"
  [[ "$REPLY_VAL" =~ ^[Yy] ]] || die "NDI license not accepted; nothing installed."
  ACCEPT=1
}

install_sdk_from_tarball() {
  local tb="$1" work inst dir
  confirm_license
  work="$(mktemp -d)"
  tar -xzf "$tb" -C "$work"
  inst="$(find "$work" -maxdepth 2 -type f -name 'Install_NDI_SDK*.sh' | head -n1)"
  [[ -n "$inst" ]] || die "No installer script found inside $tb"
  # The NDI installer shows its license and waits for "y"; feed it that answer.
  (cd "$work" && PAGER=cat bash "$inst" < <(yes) >/dev/null 2>&1) || true
  dir="$(find "$work" -maxdepth 2 -type d -name 'NDI SDK for Linux' | head -n1)"
  [[ -n "$dir" ]] && sdk_ok "$dir" || die "The NDI installer did not produce an 'NDI SDK for Linux' folder. Run it by hand and re-run this with --ndi-sdk <folder>."
  rm -rf "$SDK_HOME"
  cp -a "$dir" "$SDK_HOME"
  rm -rf "$work"
}

say "NDI SDK"
if [[ -n "$SDK_DIR" ]]; then
  sdk_ok "$SDK_DIR" || die "$SDK_DIR does not contain include/Processing.NDI.Lib.h"
elif [[ -n "$TARBALL" ]]; then
  [[ -f "$TARBALL" ]] || die "$TARBALL not found"
  install_sdk_from_tarball "$TARBALL"; SDK_DIR="$SDK_HOME"
elif SDK_DIR="$(find_sdk)"; then
  echo "Found: $SDK_DIR"
else
  TARBALL="$(find_tarball || true)"
  if [[ -n "$TARBALL" ]]; then
    echo "Found download: $TARBALL"
  else
    confirm_license
    echo "Downloading the NDI SDK..."
    apt-get install -y curl ca-certificates >/dev/null
    TARBALL="$(mktemp -d)/ndi-sdk.tar.gz"
    if ! curl -fL --retry 3 -o "$TARBALL" "$NDI_URL"; then
      die "Could not download the NDI SDK automatically.
Download 'NDI SDK for Linux' yourself from:
  $NDI_PAGE
copy the .tar.gz to this Pi (for example into your home folder) and run  sudo ./install.sh  again."
    fi
  fi
  install_sdk_from_tarball "$TARBALL"; SDK_DIR="$SDK_HOME"
fi
echo "Using NDI SDK at: $SDK_DIR"

# ---------------------------------------------------------------- build + install
say "Building and installing the player (a few minutes)"
NDI_SDK_DIR="$SDK_DIR" "$REPO/scripts/setup-pi.sh"

# ---------------------------------------------------------------- settings
configure() {
  say "Settings"
  local lines=() names=() i choice

  if [[ -z "$SRC" && -z "$SRCURL" ]]; then
    echo "Looking for NDI sources on the network (5 s)..."
    mapfile -t lines < <(bdecode-player --list 2>/dev/null | grep -v -e '(none found)' || true)
    if [[ ${#lines[@]} -gt 0 ]]; then
      echo "Found these NDI sources:"
      for i in "${!lines[@]}"; do
        names[$i]="$(printf '%s' "${lines[$i]}" | sed 's/  \[.*\]$//')"
        printf '  %d) %s\n' "$((i+1))" "${lines[$i]}"
      done
      echo "  0) Take whichever source appears first"
      ask "Which one should this unit show?" "0"
      choice="$REPLY_VAL"
      if [[ "$choice" =~ ^[1-9][0-9]*$ && "$choice" -le ${#names[@]} ]]; then SRC="${names[$((choice-1))]}"; fi
    else
      echo "No NDI sources visible right now. You can type a name or leave blank for the first one found."
      ask "NDI source name (part of the name is fine):" ""
      SRC="$REPLY_VAL"
    fi
  fi

  if [[ -z "$AUDIO" ]]; then
    local hd=()
    mapfile -t hd < <(aplay -L 2>/dev/null | grep -E '^hdmi:CARD=vc4hdmi[0-9]+,DEV=0' || true)
    if [[ ${#hd[@]} -eq 0 ]]; then
      warn "No HDMI audio device found yet (a display must be connected; a reboot may be needed). Using 'default'."
      AUDIO="default"
    elif [[ ${#hd[@]} -eq 1 ]]; then
      AUDIO="${hd[0]}"
    else
      echo "HDMI audio outputs:"
      for i in "${!hd[@]}"; do printf '  %d) %s%s\n' "$((i+1))" "${hd[$i]}" "$([[ $i -eq 0 ]] && echo '  (HDMI port nearest the USB-C power)')"; done
      ask "Which HDMI port is the display on?" "1"
      choice="$REPLY_VAL"
      [[ "$choice" =~ ^[1-9][0-9]*$ && "$choice" -le ${#hd[@]} ]] || choice=1
      AUDIO="${hd[$((choice-1))]}"
    fi
  fi

  mkdir -p /config
  python3 - "$CONF" "$SRC" "$SRCURL" "$AUDIO" <<'PY'
import json, sys
path, name, url, dev = sys.argv[1:5]
try:
    cfg = json.load(open(path))
except Exception:
    cfg = {}
cfg.update({"source_name": name, "source_url": url, "audio_device": dev})
cfg.setdefault("max_output_height", 1080)
with open(path, "w") as f:
    json.dump(cfg, f, indent=2)
    f.write("\n")
PY
  echo "Saved $CONF"
}

# First run (no marker), --reconfigure, or any setting passed on the command line.
if [[ ! -f /config/.configured || $RECONF -eq 1 || -n "$SRC" || -n "$SRCURL" || -n "$AUDIO" ]]; then
  configure
fi
touch /config/.configured

# ---------------------------------------------------------------- start
say "Starting"
systemctl restart broadcastdecode-player.service
sleep 4
systemctl --no-pager --lines=0 status broadcastdecode-player.service | sed -n '1,3p' || true
echo
journalctl -u broadcastdecode-player.service -n 12 --no-pager || true

cat <<EOF

Done. The player now starts automatically on every boot.
  Logs:            journalctl -fu broadcastdecode-player
  See sources:     bdecode-player --list
  Change settings: sudo ./install.sh --reconfigure     (or edit $CONF, then restart the service)
  Remove:          sudo ./install.sh --uninstall
EOF
