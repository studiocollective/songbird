#pragma once

#include "MagentaAliases.h"
#include "MagentaConstants.h"

namespace magenta {

class CircularBuffer {
 public:
  enum class Format { Mono = 1, Stereo = 2 };

  CircularBuffer(size_t numFrames = DEFAULT_FRAME_CAPACITY,
                 Format format = Format::Stereo);

  // JUCE-native stereo operations
  bool writeStereoSample(float left, float right);
  int readStereo(juce::AudioBuffer<float> &outBuffer, int numFrames, float sampleRate = 48000);
  int readResampled(juce::AudioBuffer<float> &outBuffer, int numFrames, float sampleRate);

  void clear();

  bool isStereo() const { return numChannels == 2; }
  size_t getNumFramesAvailable() const;

 private:
  juce::AudioBuffer<float> buffer;
  const size_t frameCapacity;
  const size_t numChannels;
  size_t head;
  size_t tail;
  size_t count;
  mutable mutex mtx;
};

}  // namespace magenta
