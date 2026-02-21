#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

namespace te = tracktion;

/**
 * MidiRecorder – captures MIDI notes from an external MIDI device or the
 * computer keyboard (via a virtual MIDI device) into a Tracktion MidiClip.
 *
 * Usage:
 *   1. Call listMidiDevices() to enumerate available inputs.
 *   2. Call openDevice(name) to open the chosen input.
 *   3. Call startRecording(track, startBeat) before/at transport start.
 *   4. Call stopRecording() to finalise the clip.
 *   5. Read recordedMessages() to get the captured MIDI (for serialisation).
 */
class MidiRecorder : private juce::MidiInputCallback
{
public:
    explicit MidiRecorder(te::Edit& edit);
    ~MidiRecorder() override;

    // — Device management —
    static juce::StringArray listMidiDevices();
    bool openDevice(const juce::String& deviceName);
    void closeDevice();
    juce::String getOpenDeviceName() const { return openDeviceName; }

    // — Recording —
    void startRecording(te::AudioTrack* track, double startBeatPosition);
    void stopRecording();
    bool isRecording() const { return recording.load(); }

    // — Access raw recorded data (serialised MIDI messages with beat times) —
    struct TimedMidiMessage {
        double beatPosition;
        juce::MidiMessage message;
    };
    const juce::Array<TimedMidiMessage>& getRecordedMessages() const { return recordedMessages; }
    void clearRecordedMessages() { recordedMessages.clear(); }

    // Optional callback invoked on the message thread whenever a note is received
    std::function<void()> onNoteRecorded;

private:
    // juce::MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& msg) override;

    te::Edit& edit;
    std::unique_ptr<juce::MidiInput> midiInput;
    juce::String openDeviceName;

    std::atomic<bool> recording { false };
    double recordStartBeat = 0.0;
    te::AudioTrack* recordingTrack = nullptr;

    // Buffer for in-flight unmatched note-ons (pitch -> note ptr)
    struct PendingNote {
        double startBeat;
        int pitch;
        int velocity;
    };
    juce::Array<PendingNote> pendingNotes;
    juce::CriticalSection pendingLock;

    juce::Array<TimedMidiMessage> recordedMessages;
    juce::CriticalSection recordedLock;

    // Get current beat position from Edit transport
    double getCurrentBeat() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRecorder)
};
