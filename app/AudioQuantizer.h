#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <string>

/**
 * Quantize an audio buffer to exact bar boundaries and save as WAV.
 *
 * Trims or pads the audio so it is exactly N bars long at the given BPM,
 * applies a short fade-in/fade-out to avoid clicks, and saves to disk
 * as a 32-bit float WAV file.
 *
 * @param audio       The audio buffer to quantize
 * @param sampleRate  Sample rate of the audio (e.g. 44100, 48000)
 * @param bpm         Project BPM
 * @param targetBars  Number of bars to quantize to. 0 = auto-detect nearest.
 * @param outputDir   Directory to save the WAV file in
 * @param fileName    Base filename (without extension)
 * @return            Full path to the saved WAV file, or empty on error
 */
std::string quantizeAndSaveAudio(
    const juce::AudioBuffer<float>& audio,
    int sampleRate,
    int bpm,
    int targetBars,
    const juce::File& outputDir,
    const juce::String& fileName
);
