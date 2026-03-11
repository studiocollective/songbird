#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <functional>
#include <atomic>

namespace te = tracktion;

/**
 * MasterAnalyzerPlugin — A lightweight Tracktion internal plugin that sits
 * on the master track and taps the audio buffer for spectrum/stereo analysis.
 *
 * Also applies a lock-free gain ramp to prevent pops/clicks on transport
 * start/stop and audio device changes. The ramp is ~5ms (256 samples at 48kHz).
 */
class MasterAnalyzerPlugin : public te::Plugin
{
public:
    MasterAnalyzerPlugin(te::PluginCreationInfo info) : te::Plugin(info) {}
    ~MasterAnalyzerPlugin() override = default;

    static const char* xmlTypeName;
    static juce::ValueTree create();

    juce::String getName() const override           { return "MasterAnalyzer"; }
    juce::String getPluginType() override           { return xmlTypeName; }
    bool canBeAddedToClip() override                 { return false; }
    bool canBeAddedToRack() override                 { return false; }
    bool needsConstantBufferSize() override          { return false; }
    bool producesAudioWhenNoAudioInput() override    { return false; }

    void initialise(const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}

    void applyToBuffer(const te::PluginRenderContext& fc) override
    {
        if (fc.destBuffer == nullptr) return;

        auto& buffer = *fc.destBuffer;
        int start = fc.bufferStartSample;
        int numSamples = fc.bufferNumSamples;

        // ── Gain ramp logic (lock-free state machine) ──
        int state = rampState.load(std::memory_order_acquire);

        if (state == RampState::FadingOut)
        {
            // Linear ramp: fixed step per sample = 1/rampLength
            const float stepPerSample = 1.0f / (float)rampLength;
            int samplesToProcess = juce::jmin(numSamples, rampSamplesRemaining);

            if (samplesToProcess > 0)
            {
                float startGain = rampGain;
                float endGain = juce::jmax(0.0f, rampGain - stepPerSample * (float)samplesToProcess);
                buffer.applyGainRamp(start, samplesToProcess, startGain, endGain);
                rampGain = endGain;
                rampSamplesRemaining -= samplesToProcess;
            }

            // Silence any remaining samples after ramp completes
            if (samplesToProcess < numSamples)
                buffer.clear(start + samplesToProcess, numSamples - samplesToProcess);

            if (rampSamplesRemaining <= 0)
            {
                rampGain = 0.0f;
                rampState.store(RampState::Silent, std::memory_order_release);
            }
        }
        else if (state == RampState::Silent)
        {
            buffer.clear(start, numSamples);
        }
        else if (state == RampState::FadingIn)
        {
            const float stepPerSample = 1.0f / (float)rampLength;
            int samplesToProcess = juce::jmin(numSamples, rampSamplesRemaining);

            if (samplesToProcess > 0)
            {
                float startGain = rampGain;
                float endGain = juce::jmin(1.0f, rampGain + stepPerSample * (float)samplesToProcess);
                buffer.applyGainRamp(start, samplesToProcess, startGain, endGain);
                rampGain = endGain;
                rampSamplesRemaining -= samplesToProcess;
            }
            // Remaining samples after ramp are already at gain=1.0 (pass through)

            if (rampSamplesRemaining <= 0)
            {
                rampGain = 1.0f;
                rampState.store(RampState::Idle, std::memory_order_release);
            }
        }
        // RampState::Idle → pass through unchanged (zero overhead)

        // Forward to analysis callback (after ramp is applied)
        if (onBuffer)
            onBuffer(buffer, start, numSamples);
    }

    // ── Fade control (call from message thread) ──

    /** Request a fade-out. The ramp starts from current gain toward 0. */
    void requestFadeOut()
    {
        rampSamplesRemaining = rampLength;
        // rampGain stays at its current value (should be 1.0 from Idle)
        rampState.store(RampState::FadingOut, std::memory_order_release);
    }

    /** Request a fade-in from silence to full volume. */
    void requestFadeIn()
    {
        rampGain = 0.0f;
        rampSamplesRemaining = rampLength;
        rampState.store(RampState::FadingIn, std::memory_order_release);
    }

    /** Immediately go silent (no ramp). Used before device teardown. */
    void goSilent()
    {
        rampGain = 0.0f;
        rampState.store(RampState::Silent, std::memory_order_release);
    }

    /** Return to idle pass-through immediately. */
    void goIdle()
    {
        rampGain = 1.0f;
        rampState.store(RampState::Idle, std::memory_order_release);
    }

    bool isSilent() const { return rampState.load(std::memory_order_acquire) == RampState::Silent; }
    bool isIdle() const   { return rampState.load(std::memory_order_acquire) == RampState::Idle; }

    // Callback — set by PlaybackInfo to receive raw master audio
    std::function<void(const juce::AudioBuffer<float>&, int, int)> onBuffer;

private:
    enum RampState { Idle = 0, FadingOut = 1, Silent = 2, FadingIn = 3 };
    std::atomic<int> rampState { RampState::Silent };  // Start silent — fade in only after attachClients()

    // Only touched by the audio thread (inside applyToBuffer) once a ramp
    // is in progress. Written by requestFadeIn/requestFadeOut on the message
    // thread *before* the state transition (release fence ensures visibility).
    float rampGain = 0.0f;  // Matches Silent default
    int rampSamplesRemaining = 0;
    static constexpr int rampLength = 256;  // ~5ms at 48kHz
};
