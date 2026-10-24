#!/usr/bin/env bash
# Build and install the BroadcastDecode player on a Raspberry Pi 5 (Raspberry Pi OS Lite, 64-bit).
# Usage: sudo NDI_SDK_DIR="/home/pi/NDI SDK for Linux" ./scripts/setup-pi.sh
set -euo pipefail

if [[ $EUID -ne 0 ]]; then echo "Run with sudo."; exit 1; fi
: "${NDI_SDK_DIR:?Set NDI_SDK_DIR to the extracted NDI SDK folder (see docs/BUILD.md)}"

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Pick the NDI library folder that exists in this SDK.
NDI_LIB_ARCH=""
for d in aarch64-rpi4-linux-gnueabi aarch64-linux-gnu; do
  if [[ -d "$NDI_SDK_DIR/lib/$d" ]]; then NDI_LIB_ARCH="$d"; break; fi
done
[[ -n "$NDI_LIB_ARCH" ]] || { echo "No aarch64 folder under $NDI_SDK_DIR/lib"; exit 1; }
echo "Using NDI libs: $NDI_LIB_ARCH"

apt-get update
apt-get install -y build-essential cmake pkg-config libdrm-dev libasound2-dev nlohmann-json3-dev \
  avahi-daemon libavcodec-dev libavutil-dev   # avahi = NDI discovery; ffmpeg libs = HX decoding

cmake -S "$REPO" -B "$REPO/build" -DNDI_SDK_DIR="$NDI_SDK_DIR" -DNDI_LIB_ARCH="$NDI_LIB_ARCH"
cmake --build "$REPO/build" -j"$(nproc)"
cmake --install "$REPO/build"

# NDI runtime library (from the SDK you downloaded; not stored in this repo).
cp -a "$NDI_SDK_DIR/lib/$NDI_LIB_ARCH"/libndi.so* /usr/local/lib/
ldconfig

mkdir -p /config
[[ -f /config/config.json ]] || cp "$REPO/config/config.example.json" /config/config.json

cp "$REPO/systemd/broadcastdecode-player.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable broadcastdecode-player.service

echo
echo "Installed. Next:"
echo "  bdecode-player --list                 # see which NDI sources are visible"
echo "  nano /config/config.json              # set source_name (and audio_device)"
echo "  sudo systemctl start broadcastdecode-player && journalctl -fu broadcastdecode-player"
