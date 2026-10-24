#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <alsa/asoundlib.h>

// Plays NDI audio (planar float) to an ALSA device as stereo S16.
// Prototype: only the first two NDI channels are played (mono is duplicated).
class AudioOut {
public:
  explicit AudioOut(std::string device) : dev_(std::move(device)) {}
  ~AudioOut() { close(); }

  // planar: channel c starts at planar + c * strideSamples.
  void write(const float* planar, int channels, int samples, int sampleRate, int strideSamples);

private:
  bool reopen(int sampleRate);
  void close();

  std::string dev_;
  snd_pcm_t* pcm_ = nullptr;
  int rate_ = 0;
  std::vector<int16_t> buf_;
};
