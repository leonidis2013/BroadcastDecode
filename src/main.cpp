// BroadcastDecode player (prototype): NDI in -> DRM/KMS video + ALSA audio out.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <Processing.NDI.Lib.h>

#include "audio.h"
#include "config.h"
#include "display.h"

using Clock = std::chrono::steady_clock;

static std::atomic<bool> g_run{true};
static void onSignal(int) { g_run = false; }

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

struct Chosen {
  std::string name;
  std::string url;
};

// Waits until the configured source shows up (or the first source if none is configured).
static bool chooseSource(NDIlib_find_instance_t find, const Config& cfg, Chosen& out) {
  if (!cfg.source_url.empty()) {
    out.name = cfg.source_name;
    out.url = cfg.source_url;
    return true;
  }
  const std::string want = lower(cfg.source_name);
  while (g_run) {
    NDIlib_find_wait_for_sources(find, 1000);
    uint32_t n = 0;
    const NDIlib_source_t* srcs = NDIlib_find_get_current_sources(find, &n);
    const NDIlib_source_t* hit = nullptr;
    for (uint32_t i = 0; i < n; i++) {
      if (want.empty() || lower(srcs[i].p_ndi_name ? srcs[i].p_ndi_name : "").find(want) != std::string::npos) {
        hit = &srcs[i];
        break;
      }
    }
    if (hit) {
      out.name = hit->p_ndi_name ? hit->p_ndi_name : "";
      out.url = hit->p_url_address ? hit->p_url_address : "";
      return true;
    }
    std::fprintf(stderr, "waiting for NDI source '%s' (%u visible)\n", cfg.source_name.c_str(), n);
  }
  return false;
}

static int listSources() {
  NDIlib_find_create_t fc{};
  NDIlib_find_instance_t find = NDIlib_find_create_v2(&fc);
  if (!find) return 1;
  std::fprintf(stderr, "searching for NDI sources (5 s)...\n");
  NDIlib_find_wait_for_sources(find, 5000);
  uint32_t n = 0;
  const NDIlib_source_t* srcs = NDIlib_find_get_current_sources(find, &n);
  for (uint32_t i = 0; i < n; i++)
    std::printf("%s  [%s]\n", srcs[i].p_ndi_name ? srcs[i].p_ndi_name : "?",
                srcs[i].p_url_address ? srcs[i].p_url_address : "?");
  if (n == 0) std::printf("(none found)\n");
  NDIlib_find_destroy(find);
  return 0;
}

int main(int argc, char** argv) {
  std::string configPath = "/config/config.json";
  bool listOnly = false;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--config") && i + 1 < argc) configPath = argv[++i];
    else if (!std::strcmp(argv[i], "--list")) listOnly = true;
    else if (!std::strcmp(argv[i], "--version")) {
      std::printf("bdecode-player %s\n", BDECODE_VERSION);
      return 0;
    } else {
      std::fprintf(stderr, "usage: %s [--config path] [--list] [--version]\n", argv[0]);
      return 2;
    }
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  if (!NDIlib_initialize()) {
    std::fprintf(stderr, "NDI: this CPU is not supported or the NDI runtime failed to start\n");
    return 1;
  }
  if (listOnly) {
    int rc = listSources();
    NDIlib_destroy();
    return rc;
  }

  const Config cfg = loadConfig(configPath);

  Display display;
  if (!display.open(cfg.max_output_height)) {
    NDIlib_destroy();
    return 1;
  }
  display.drawColorBars();
  display.present();

  NDIlib_find_create_t fc{};
  NDIlib_find_instance_t find = NDIlib_find_create_v2(&fc);
  Chosen chosen;
  if (!find || !chooseSource(find, cfg, chosen)) {
    if (find) NDIlib_find_destroy(find);
    NDIlib_destroy();
    return g_run ? 1 : 0;
  }
  std::fprintf(stderr, "connecting to '%s' %s\n", chosen.name.c_str(), chosen.url.c_str());

  NDIlib_source_t src{};
  src.p_ndi_name = chosen.name.empty() ? nullptr : chosen.name.c_str();
  src.p_url_address = chosen.url.empty() ? nullptr : chosen.url.c_str();

  NDIlib_recv_create_v3_t rc{};
  rc.source_to_connect_to = src;
  rc.color_format = NDIlib_recv_color_format_BGRX_BGRA;
  rc.bandwidth = NDIlib_recv_bandwidth_highest;
  rc.allow_video_fields = false;
  rc.p_ndi_recv_name = "BroadcastDecode";
  NDIlib_recv_instance_t recv = NDIlib_recv_create_v3(&rc);
  if (!recv) {
    std::fprintf(stderr, "NDI: could not create receiver\n");
    NDIlib_find_destroy(find);
    NDIlib_destroy();
    return 1;
  }

  // Audio on its own thread so a slow ALSA write never stalls video.
  // (NDI receivers may be captured from several threads at once.)
  std::thread audioThread([&] {
    AudioOut audio(cfg.audio_device);
    while (g_run) {
      NDIlib_audio_frame_v3_t a{};
      if (NDIlib_recv_capture_v3(recv, nullptr, &a, nullptr, 100) == NDIlib_frame_type_audio) {
        if (a.FourCC == NDIlib_FourCC_audio_type_FLTP) {
          audio.write(reinterpret_cast<const float*>(a.p_data), a.no_channels, a.no_samples, a.sample_rate,
                      (int)(a.channel_stride_in_bytes / sizeof(float)));
        }
        NDIlib_recv_free_audio_v3(recv, &a);
      }
    }
  });

  auto lastFrame = Clock::now();
  bool noSignal = true;  // color bars are already on screen
  uint64_t frames = 0;
  auto statsAt = Clock::now();

  while (g_run) {
    NDIlib_video_frame_v2_t v{};
    if (NDIlib_recv_capture_v3(recv, &v, nullptr, nullptr, 100) == NDIlib_frame_type_video) {
      if (v.FourCC == NDIlib_FourCC_video_type_BGRX || v.FourCC == NDIlib_FourCC_video_type_BGRA) {
        display.drawBGRX(v.p_data, v.xres, v.yres, v.line_stride_in_bytes);
        display.present();
        frames++;
        lastFrame = Clock::now();
        noSignal = false;
      }
      NDIlib_recv_free_video_v2(recv, &v);
    } else if (!noSignal && Clock::now() - lastFrame > std::chrono::seconds(2)) {
      display.drawColorBars();
      display.present();
      noSignal = true;
      std::fprintf(stderr, "no video for 2 s, showing color bars (NDI reconnects automatically)\n");
    }

    if (Clock::now() - statsAt > std::chrono::seconds(10)) {
      std::fprintf(stderr, "video: %.1f fps\n",
                   frames / std::chrono::duration<double>(Clock::now() - statsAt).count());
      frames = 0;
      statsAt = Clock::now();
    }
  }

  audioThread.join();
  NDIlib_recv_destroy(recv);
  NDIlib_find_destroy(find);
  NDIlib_destroy();
  return 0;
}
