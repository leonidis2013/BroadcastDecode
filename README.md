# BroadcastDecode

A headless Raspberry Pi 5 appliance that decodes an NDI feed and outputs it to HDMI (video + embedded audio). It boots straight into the player, with no desktop, and is configured entirely from a web page.

Part of the Live / Broadcast series of tools.

- Status: v0.2.0 prototype player (NDI in, HDMI video + audio out); not yet tested on hardware. No web page yet
- Build and test: see [docs/BUILD.md](docs/BUILD.md)
- Target hardware: Raspberry Pi 5, Raspberry Pi OS Lite (Trixie, 64-bit)
- NDI: NDI SDK 6.3.x (High Bandwidth, HX2, HX3 decode)
- Output: 1080p60 target, 4K as a stretch goal (tested, not promised)
- Versioning: major.minor.patch, committed directly to `main`

See [docs/DESIGN.md](docs/DESIGN.md) for the full design.

Copyright (c) Jerod Ulicki. NDI is a trademark of Vizrt NDI AB; the NDI SDK is used under its own license terms and is not redistributed in this repository.
