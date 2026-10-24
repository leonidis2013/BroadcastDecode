#pragma once
#include <cstddef>
#include <cstdint>
#include <xf86drmMode.h>

// Direct DRM/KMS output: two dumb buffers, page-flipped on vblank. No X/Wayland.
class Display {
public:
  ~Display() { close(); }

  // Finds the first connected HDMI/DP output and picks a mode no taller than maxHeight
  // (progressive, <= 60 Hz).
  bool open(int maxHeight);
  void close();

  int width() const { return mode_.hdisplay; }
  int height() const { return mode_.vdisplay; }

  // Draw into the back buffer. Source must be 32-bit BGRX/BGRA (byte order B,G,R,X).
  // Same size as the output = straight copy; otherwise scaled to fit with black bars (nearest neighbour).
  void drawBGRX(const uint8_t* src, int srcW, int srcH, int srcStrideBytes);
  void drawColorBars();

  // Flip the back buffer to the screen and wait for vblank.
  void present();

private:
  struct Buf {
    uint32_t handle = 0;
    uint32_t fb = 0;
    uint32_t pitch = 0;
    size_t size = 0;
    uint8_t* map = nullptr;
  };

  bool createBuf(Buf& b);
  void destroyBuf(Buf& b);
  static void flipHandler(int fd, unsigned seq, unsigned sec, unsigned usec, void* data);

  int fd_ = -1;
  uint32_t conn_ = 0;
  uint32_t crtc_ = 0;
  drmModeModeInfo mode_{};
  Buf bufs_[2];
  int front_ = 0;
  volatile bool flipDone_ = true;
};
