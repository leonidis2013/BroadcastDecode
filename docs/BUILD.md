# Building and testing BroadcastDecode (v0.2.0 prototype)

The prototype is only the player: NDI in, HDMI video + audio out. There is no web page yet.
It has not been run on hardware yet, so expect a first round of fixes.

## Quick install (recommended)
On the Pi, with Ethernet and (ideally) the display connected:
```
sudo apt-get install -y git
git clone https://github.com/leonidis2013/BroadcastDecode
cd BroadcastDecode
sudo ./install.sh
```
The installer finds or downloads the NDI SDK (it asks you to accept NDI's license), builds the player,
shows the NDI sources it can see so you can pick one, picks the HDMI audio output, and starts the service.
If the automatic SDK download fails, download "NDI SDK for Linux" from the NDI site, put the `.tar.gz` in
your home folder, and run `sudo ./install.sh` again. Update later with `git pull && sudo ./install.sh`.
`./install.sh --help` lists the options (`--reconfigure`, `--uninstall`, ...).

The manual steps below do the same thing by hand.

## You need
- Raspberry Pi 5 with Raspberry Pi OS Lite 64-bit (Trixie), wired Ethernet, an HDMI display or capture device
- The NDI SDK for Linux (free, but you must download it yourself and accept NDI's license):
  https://ndi.video/for-developers/ndi-sdk/download/
- An NDI source on the same subnet (NDI Test Patterns from NDI Tools works well)

## Steps
1. Copy the SDK download to the Pi, extract it, and run the installer script it contains. This produces
   a folder such as `NDI SDK for Linux` containing `include/` and `lib/`.
2. Get the code:
   ```
   git clone https://github.com/leonidis2013/BroadcastDecode
   cd BroadcastDecode
   ```
3. Build and install (one command):
   ```
   sudo NDI_SDK_DIR="/home/<you>/NDI SDK for Linux" ./scripts/setup-pi.sh
   ```
4. See what the Pi can discover: `bdecode-player --list`
5. Edit `/config/config.json` and set `source_name` (any part of the name works, case-insensitive).
   Leave it empty to take the first source found.
6. Run it by hand first (stop the service if it is running): `sudo bdecode-player`
   You should see color bars, then video once connected. Ctrl+C to quit.
7. When happy: `sudo systemctl start broadcastdecode-player`. It also starts on every boot.

## Config keys
| Key | Meaning |
|---|---|
| `source_name` | NDI source to show. Empty = first found |
| `source_url` | Optional `ip[:port]` for a source on another subnet or VLAN |
| `audio_device` | ALSA device. Try `default`, then `hdmi:CARD=vc4hdmi0,DEV=0` (`aplay -L` lists them) |
| `max_output_height` | Largest display mode height to use (1080 default) |

## Things to report back after the first run
- Does `--list` show your source? (If not: same subnet? avahi running?)
- Frame rate printed every 10 s, and CPU use (`top`) for High Bandwidth and for HX sources
- Is HDMI audio heard, and which `audio_device` worked
- Any message from `display:` or `audio:` lines
- Temperature under load: `vcgencmd measure_temp`

## Known prototype limits
- Audio plays only the first two NDI channels
- Non-matching source sizes are scaled in software (nearest neighbour), which is slow; match the output when testing
- Runs as root, no web page, config is read only at start (restart the service after editing)
- The NDI library folder name differs between SDK versions; `setup-pi.sh` tries `aarch64-rpi4-linux-gnueabi`, then `aarch64-linux-gnu`
