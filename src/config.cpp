#include "config.h"

#include <fstream>
#include <cstdio>
#include <nlohmann/json.hpp>

Config loadConfig(const std::string& path) {
  Config cfg;
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "config: %s not found, using defaults\n", path.c_str());
    return cfg;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(in);
    cfg.source_name = j.value("source_name", cfg.source_name);
    cfg.source_url = j.value("source_url", cfg.source_url);
    cfg.audio_device = j.value("audio_device", cfg.audio_device);
    cfg.max_output_height = j.value("max_output_height", cfg.max_output_height);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "config: could not parse %s (%s), using defaults\n", path.c_str(), e.what());
  }
  return cfg;
}
