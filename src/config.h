#pragma once
#include <string>

struct Config {
  std::string source_name;            // NDI source to show (exact or case-insensitive substring). Empty = first found.
  std::string source_url;             // Optional direct address (ip[:port]) for sources on other subnets.
  std::string audio_device = "default"; // ALSA device. For HDMI on a Pi 5 try "hdmi:CARD=vc4hdmi0,DEV=0".
  int max_output_height = 1080;       // Never pick a display mode taller than this.
};

// Loads config from a JSON file. Missing file or keys fall back to defaults.
Config loadConfig(const std::string& path);
