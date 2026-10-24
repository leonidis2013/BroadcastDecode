#include "display.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

static int pickMode(const drmModeConnector* c, int maxHeight) {
  int best = -1;
  long bestScore = -1;
  for (int i = 0; i < c->count_modes; i++) {
    const drmModeModeInfo& m = c->modes[i];
    if (m.vdisplay > maxHeight) continue;
    if (m.vrefresh > 60) continue;
    if (m.flags & DRM_MODE_FLAG_INTERLACE) continue;
    long score = (long)m.hdisplay * m.vdisplay * 1000 + m.vrefresh * 10 +
                 ((m.type & DRM_MODE_TYPE_PREFERRED) ? 5 : 0);
    if (score > bestScore) {
      bestScore = score;
      best = i;
    }
  }
  return best;
}

bool Display::open(int maxHeight) {
  glob_t g;
  if (glob("/dev/dri/card*", 0, nullptr, &g) != 0) {
    std::fprintf(stderr, "display: no DRM devices found\n");
    return false;
  }

  bool ok = false;
  for (size_t i = 0; i < g.gl_pathc && !ok; i++) {
    int fd = ::open(g.gl_pathv[i], O_RDWR | O_CLOEXEC);
    if (fd < 0) continue;
    drmModeRes* res = drmModeGetResources(fd);
    if (!res) {
      ::close(fd);
      continue;
    }
    for (int c = 0; c < res->count_connectors && !ok; c++) {
      drmModeConnector* conn = drmModeGetConnector(fd, res->connectors[c]);
      if (!conn) continue;
      if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
        int best = pickMode(conn, maxHeight);
        uint32_t crtc = 0;
        if (conn->encoder_id) {
          drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoder_id);
          if (enc) {
            crtc = enc->crtc_id;
            drmModeFreeEncoder(enc);
          }
        }
        for (int e = 0; e < conn->count_encoders && !crtc; e++) {
          drmModeEncoder* enc = drmModeGetEncoder(fd, conn->encoders[e]);
          if (!enc) continue;
          for (int k = 0; k < res->count_crtcs; k++) {
            if (enc->possible_crtcs & (1u << k)) {
              crtc = res->crtcs[k];
              break;
            }
          }
          drmModeFreeEncoder(enc);
        }
        if (best >= 0 && crtc) {
          fd_ = fd;
          conn_ = conn->connector_id;
          crtc_ = crtc;
          mode_ = conn->modes[best];
          ok = true;
        }
      }
      drmModeFreeConnector(conn);
    }
    drmModeFreeResources(res);
    if (!ok) ::close(fd);
  }
  globfree(&g);

  if (!ok) {
    std::fprintf(stderr, "display: no connected output with a usable mode (is a display plugged in?)\n");
    return false;
  }

  for (auto& b : bufs_) {
    if (!createBuf(b)) {
      close();
      return false;
    }
  }

  if (drmModeSetCrtc(fd_, crtc_, bufs_[0].fb, 0, 0, &conn_, 1, &mode_) != 0) {
    std::fprintf(stderr, "display: drmModeSetCrtc failed (another process may own the display)\n");
    close();
    return false;
  }
  front_ = 0;
  std::fprintf(stderr, "display: %dx%d@%d\n", width(), height(), (int)mode_.vrefresh);
  return true;
}

void Display::close() {
  if (fd_ < 0) return;
  for (auto& b : bufs_) destroyBuf(b);
  ::close(fd_);
  fd_ = -1;
}

bool Display::createBuf(Buf& b) {
  drm_mode_create_dumb creq{};
  creq.width = mode_.hdisplay;
  creq.height = mode_.vdisplay;
  creq.bpp = 32;
  if (drmIoctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
    std::fprintf(stderr, "display: create dumb buffer failed\n");
    return false;
  }
  b.handle = creq.handle;
  b.pitch = creq.pitch;
  b.size = creq.size;

  if (drmModeAddFB(fd_, mode_.hdisplay, mode_.vdisplay, 24, 32, b.pitch, b.handle, &b.fb) != 0) {
    std::fprintf(stderr, "display: AddFB failed\n");
    return false;
  }

  drm_mode_map_dumb mreq{};
  mreq.handle = b.handle;
  if (drmIoctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
    std::fprintf(stderr, "display: map dumb failed\n");
    return false;
  }
  void* p = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, mreq.offset);
  if (p == MAP_FAILED) {
    std::fprintf(stderr, "display: mmap failed\n");
    return false;
  }
  b.map = static_cast<uint8_t*>(p);
  std::memset(b.map, 0, b.size);
  return true;
}

void Display::destroyBuf(Buf& b) {
  if (b.map) munmap(b.map, b.size);
  if (b.fb) drmModeRmFB(fd_, b.fb);
  if (b.handle) {
    drm_mode_destroy_dumb dreq{};
    dreq.handle = b.handle;
    drmIoctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
  }
  b = Buf{};
}

void Display::drawBGRX(const uint8_t* src, int sw, int sh, int srcStride) {
  Buf& b = bufs_[1 - front_];
  const int dw = width();
  const int dh = height();

  if (sw == dw && sh == dh) {
    for (int y = 0; y < dh; y++)
      std::memcpy(b.map + (size_t)y * b.pitch, src + (size_t)y * srcStride, (size_t)dw * 4);
    return;
  }

  std::memset(b.map, 0, b.size);
  const double s = std::min((double)dw / sw, (double)dh / sh);
  const int ow = std::max(1, (int)(sw * s));
  const int oh = std::max(1, (int)(sh * s));
  const int ox = (dw - ow) / 2;
  const int oy = (dh - oh) / 2;

  std::vector<int> xmap(ow);
  for (int x = 0; x < ow; x++) xmap[x] = std::min(sw - 1, (int)((long)x * sw / ow));

  for (int y = 0; y < oh; y++) {
    const int sy = std::min(sh - 1, (int)((long)y * sh / oh));
    const uint32_t* srow = reinterpret_cast<const uint32_t*>(src + (size_t)sy * srcStride);
    uint32_t* drow = reinterpret_cast<uint32_t*>(b.map + (size_t)(oy + y) * b.pitch) + ox;
    for (int x = 0; x < ow; x++) drow[x] = srow[xmap[x]];
  }
}

void Display::drawColorBars() {
  static const uint32_t bars[8] = {0x00FFFFFF, 0x00FFFF00, 0x0000FFFF, 0x0000FF00,
                                   0x00FF00FF, 0x00FF0000, 0x000000FF, 0x00000000};
  Buf& b = bufs_[1 - front_];
  const int dw = width();
  const int dh = height();
  for (int y = 0; y < dh; y++) {
    uint32_t* row = reinterpret_cast<uint32_t*>(b.map + (size_t)y * b.pitch);
    for (int x = 0; x < dw; x++) row[x] = bars[x * 8 / dw];
  }
}

void Display::flipHandler(int, unsigned, unsigned, unsigned, void* data) {
  static_cast<Display*>(data)->flipDone_ = true;
}

void Display::present() {
  const int back = 1 - front_;
  flipDone_ = false;
  if (drmModePageFlip(fd_, crtc_, bufs_[back].fb, DRM_MODE_PAGE_FLIP_EVENT, this) == 0) {
    drmEventContext ev{};
    ev.version = 2;
    ev.page_flip_handler = &Display::flipHandler;
    while (!flipDone_) {
      pollfd p{fd_, POLLIN, 0};
      if (poll(&p, 1, 100) <= 0) break;
      drmHandleEvent(fd_, &ev);
    }
  } else {
    // A previous flip was still pending; set the frame directly.
    drmModeSetCrtc(fd_, crtc_, bufs_[back].fb, 0, 0, &conn_, 1, &mode_);
  }
  front_ = back;
}
