# BroadcastDecode — Design (v0.1.0)

## 1. Goal
A small, headless, auto-booting NDI-to-HDMI decoder. Power it on, it connects to the last-selected NDI source and shows clean video with HDMI audio. All setup happens from a web page.

## 2. Decisions so far
| Area | Decision |
|---|---|
| Hardware | Raspberry Pi 5, active cooling; PoE+ possible (needs PoE+ class HAT and a capable switch port) |
| OS | Raspberry Pi OS Lite 64-bit (Trixie), read-only root, separate config partition |
| NDI | NDI SDK 6.3.x (current at time of writing: 6.3.2). HX decode on Linux relies on FFmpeg 7.x libraries — pin and verify on the image |
| Video | Mixed sources (High Bandwidth and HX). 1080p60 is the ceiling for v1; 4K attempted behind a "max output" setting after testing |
| Audio | HDMI embedded audio only |
| Latency | Balanced: small fixed buffer, favor stability |
| Player | Native C++ receiver, DRM/KMS direct output, no X/Wayland. Clean video only (no overlays) |
| Source selection | Pick from discovered list; plus optional manual IP / NDI Discovery Server for other subnets/VLANs |
| Web UI | One login; same look/feel as LivePi and Live-series plugins (dark/light, tabs, touch friendly) |
| No-signal screen | Selectable built-in screens (black, color bars, status slate) and an uploaded custom logo |
| Updates | (a) GitHub-built zip uploaded through the web page (offline); (b) OS + package update button, grayed out with "no internet access" when offline |
| Login | Single login; installer asks Y/N for a password; trusted-network skip option (as in LivePi) |
| Fleet | 2–3 units from one image; hostname and settings set per unit in the web page |
| Repo | github.com/leonidis2013/BroadcastDecode, commits straight to `main`, versions major.minor.patch |

## 3. Architecture
```
 NDI sender(s) --LAN--> [ bdecode-player ]  --DRM/KMS--> HDMI (video + audio)
                              ^   |
                  config.json |   | status (unix socket)
                              |   v
                        [ bdecode-web ]  <-- browser (phone / iPad / PC)
```
- **bdecode-player** (C++): NDI finder + receiver, decode, present frames via DRM/KMS, audio via ALSA to the HDMI device, no-signal rendering. Reads config on start and on signal; reports state over a local socket.
- **bdecode-web** (small service): login, source list, settings, uploads, update handling. Writes config atomically; never touches the video path.
- **systemd**: both services start on boot; player is restarted on failure; hardware watchdog enabled.
- **Storage**: root read-only (overlay); `/config` partition holds config.json, logo, hostname.

## 4. Web UI tabs
1. **Status** — connected source, resolution/fps, format (HB/HX), audio, network, temperature.
2. **Source** — discovered list, selected source, manual IP / Discovery Server field.
3. **Output** — resolution, scaling mode, max-output (4K attempt) toggle.
4. **No Signal** — choose screen, upload logo (size limit, auto-scale).
5. **Network** — DHCP / static IP, hostname.
6. **System** — reboot, offline zip update, OS/package update (grayed with message if no internet), login settings, version.

## 5. Risks / things to test first
1. HX decode cost on Pi 5 CPU at 1080p60 (and whether FFmpeg 7.x is available on the pinned OS).
2. High Bandwidth 1080p60 CPU headroom, thermal behavior under PoE+ HAT and case.
3. 4K feasibility (likely limited; decide after measurements).
4. NDI discovery across VLANs (mDNS does not cross subnets); validate Discovery Server / manual IP path.
5. HDMI audio behavior with the target displays/switchers (sample rate, channel count, EDID).
6. NDI SDK license terms for distribution; SDK is downloaded at build time, not stored in the repo.

## 6. Milestones
- **0.1.0** Design document (this).
- **0.2.0** Prototype receiver: discover, connect, show 1080p on HDMI with audio.
- **0.3.0** Config file + systemd auto-boot + no-signal screens.
- **0.4.0** Web UI (status, source, output, network).
- **0.5.0** Updates (zip + OS), login, read-only root.
- **0.6.0** Flashable image, per-unit hostname, PoE/thermal validation.
- **1.0.0** Field-tested on 2–3 units.

## 7. Open items
- 4K go/no-go after prototype measurements.
- Exact PoE+ HAT model and case.
