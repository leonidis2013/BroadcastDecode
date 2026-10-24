#include "audio.h"

#include <algorithm>
#include <cstdio>

bool AudioOut::reopen(int sampleRate) {
  close();
  int err = snd_pcm_open(&pcm_, dev_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    std::fprintf(stderr, "audio: cannot open '%s': %s\n", dev_.c_str(), snd_strerror(err));
    pcm_ = nullptr;
    return false;
  }
  // 100 ms of latency, ALSA resamples if the device cannot do this rate.
  err = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                           (unsigned)sampleRate, 1, 100000);
  if (err < 0) {
    std::fprintf(stderr, "audio: set_params failed: %s\n", snd_strerror(err));
    close();
    return false;
  }
  rate_ = sampleRate;
  std::fprintf(stderr, "audio: %s @ %d Hz\n", dev_.c_str(), rate_);
  return true;
}

void AudioOut::close() {
  if (pcm_) {
    snd_pcm_drain(pcm_);
    snd_pcm_close(pcm_);
    pcm_ = nullptr;
  }
  rate_ = 0;
}

void AudioOut::write(const float* planar, int channels, int samples, int sampleRate, int strideSamples) {
  if (samples <= 0 || channels <= 0) return;
  if (!pcm_ || rate_ != sampleRate) {
    if (!reopen(sampleRate)) return;
  }

  buf_.resize((size_t)samples * 2);
  const float* l = planar;
  const float* r = channels > 1 ? planar + strideSamples : planar;
  auto conv = [](float v) {
    v = std::max(-1.0f, std::min(1.0f, v));
    return (int16_t)(v * 32767.0f);
  };
  for (int i = 0; i < samples; i++) {
    buf_[(size_t)i * 2] = conv(l[i]);
    buf_[(size_t)i * 2 + 1] = conv(r[i]);
  }

  const int16_t* p = buf_.data();
  snd_pcm_sframes_t left = samples;
  while (left > 0) {
    snd_pcm_sframes_t n = snd_pcm_writei(pcm_, p, left);
    if (n < 0) {
      n = snd_pcm_recover(pcm_, (int)n, 1);
      if (n < 0) {
        std::fprintf(stderr, "audio: write failed: %s\n", snd_strerror((int)n));
        close();
        return;
      }
      continue;
    }
    p += n * 2;
    left -= n;
  }
}
