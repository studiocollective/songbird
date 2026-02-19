#include "CircularBuffer.h"

namespace magenta {

CircularBuffer::CircularBuffer(size_t numFrames, Format format)
    : buffer(static_cast<int>(static_cast<size_t>(format)),
             static_cast<int>(numFrames)),
      frameCapacity(numFrames),
      numChannels(static_cast<size_t>(format)),
      head(0),
      tail(0),
      count(0) {}

bool CircularBuffer::writeStereoSample(float left, float right) {
  lock_guard<mutex> lock(mtx);

  const int writeIndex = static_cast<int>(tail);
  buffer.setSample(0, writeIndex, left);
  buffer.setSample(1, writeIndex, right);
  tail = (tail + 1) % frameCapacity;

  count++;
  return true;
}

int CircularBuffer::readStereo(juce::AudioBuffer<float> &outBuffer, int numFrames, float sampleRate) {
  if (sampleRate != 48000) {
    return readResampled(outBuffer, numFrames, sampleRate);
  }

  lock_guard<mutex> lock(mtx);
  if (static_cast<size_t>(numFrames) > count ||
      numFrames > outBuffer.getNumSamples()) {
    return 0;
  }
  for (int i = 0; i < numFrames; ++i) {
    const int readIndex = static_cast<int>(head);
    outBuffer.setSample(0, i, buffer.getSample(0, readIndex));
    outBuffer.setSample(1, i, buffer.getSample(1, readIndex));
    head = (head + 1) % frameCapacity;
  }
  count -= numFrames;
  return numFrames;
}

int CircularBuffer::readResampled(juce::AudioBuffer<float>& outBuffer, int numFrames, float sampleRate) {
    lock_guard<mutex> lock(mtx);

    const float bufferSampleRate = SOURCE_SAMPLE_RATE;
    const double ratio = static_cast<double>(bufferSampleRate) / sampleRate;
    const int numFramesToRead = static_cast<int>(std::ceil(numFrames * ratio));

    if (static_cast<size_t>(numFramesToRead) > count || numFrames > outBuffer.getNumSamples()) {
        return 0;
    }

    for (int i = 0; i < numFrames; ++i) {
        double sourceIndexFloat = i * ratio;
        int sourceIndex = static_cast<int>(sourceIndexFloat);
        double fraction = sourceIndexFloat - sourceIndex;

        int readPos1 = (head + sourceIndex) % frameCapacity;
        int readPos2 = (head + sourceIndex + 1) % frameCapacity;

        for (int channel = 0; channel < outBuffer.getNumChannels(); ++channel) {
            float sample1 = buffer.getSample(channel, readPos1);
            float sample2 = buffer.getSample(channel, readPos2);
            float interpolatedSample = sample1 + static_cast<float>(fraction) * (sample2 - sample1);
            outBuffer.setSample(channel, i, interpolatedSample);
        }
    }

    head = (head + numFramesToRead) % frameCapacity;
    count -= numFramesToRead;

    return numFramesToRead;
}

void CircularBuffer::clear() {
    lock_guard<mutex> lock(mtx);
    if (count != 0) buffer.clear();
    head = 0;
    tail = 0;
    count = 0;
}

size_t CircularBuffer::getNumFramesAvailable() const {
  lock_guard<mutex> lock(mtx);
  return count;
}

}  // namespace magenta
