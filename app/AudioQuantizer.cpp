#include "AudioQuantizer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

std::string quantizeAndSaveAudio(
    const juce::AudioBuffer<float>& audio,
    int sampleRate,
    int bpm,
    int targetBars,
    const juce::File& outputDir,
    const juce::String& fileName)
{
    if (audio.getNumSamples() == 0 || sampleRate <= 0 || bpm <= 0)
        return {};

    // Calculate exact sample count for one bar
    // bar = 4 beats, beat = 60/bpm seconds
    double secondsPerBar = (60.0 / bpm) * 4.0;
    int samplesPerBar = static_cast<int>(std::round(secondsPerBar * sampleRate));

    // Auto-detect bar count if not specified
    if (targetBars <= 0) {
        targetBars = std::max(1, static_cast<int>(std::round(
            static_cast<double>(audio.getNumSamples()) / samplesPerBar
        )));
    }

    int targetSamples = targetBars * samplesPerBar;
    int numChannels = audio.getNumChannels();

    // Create quantized buffer (trim or pad)
    juce::AudioBuffer<float> quantized(numChannels, targetSamples);
    quantized.clear();

    int samplesToCopy = std::min(audio.getNumSamples(), targetSamples);
    for (int ch = 0; ch < numChannels; ++ch) {
        quantized.copyFrom(ch, 0, audio, ch, 0, samplesToCopy);
    }

    // Apply short fade-in and fade-out to avoid clicks (10ms)
    int fadeSamples = std::min(static_cast<int>(sampleRate * 0.01), targetSamples / 4);

    for (int ch = 0; ch < numChannels; ++ch) {
        // Fade in
        for (int i = 0; i < fadeSamples; ++i) {
            float gain = static_cast<float>(i) / fadeSamples;
            quantized.setSample(ch, i, quantized.getSample(ch, i) * gain);
        }

        // Fade out
        for (int i = 0; i < fadeSamples; ++i) {
            int idx = targetSamples - 1 - i;
            float gain = static_cast<float>(i) / fadeSamples;
            quantized.setSample(ch, idx, quantized.getSample(ch, idx) * gain);
        }
    }

    // Ensure output directory exists
    if (!outputDir.exists())
        outputDir.createDirectory();

    // Save as WAV
    juce::File outputFile = outputDir.getChildFile(fileName + ".wav");

    // Use a unique name if file exists
    int suffix = 1;
    while (outputFile.exists()) {
        outputFile = outputDir.getChildFile(fileName + "_" + juce::String(suffix++) + ".wav");
    }

    std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());
    if (!stream)
        return {};

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sampleRate, numChannels, 32, {}, 0)
    );

    if (!writer)
        return {};

    // Writer takes ownership of stream
    stream.release();

    writer->writeFromAudioSampleBuffer(quantized, 0, targetSamples);

    return outputFile.getFullPathName().toStdString();
}
