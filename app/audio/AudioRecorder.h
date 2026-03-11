#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>

namespace te = tracktion;

/**
 * AudioRecorder – captures audio from a hardware input device or loopback
 * from another track (via AuxSend/Return) into a WaveAudioClip.
 *
 * Usage:
 *   1. listAudioInputs() – returns hardware input device names.
 *   2. addAudioTrack() – creates a new audio track in the Edit.
 *   3. setRecordSource(trackId, HardwareInput | TrackLoopback, srcTrackId)
 *   4. startRecording(trackId) / stopRecording(trackId)
 */
class AudioRecorder
{
public:
    enum class SourceType { HardwareInput, TrackLoopback };

    struct AudioTrackInfo {
        int  trackId;
        juce::String sourceName;   // device name or "Loopback: <trackname>"
        SourceType   sourceType   { SourceType::HardwareInput };
        int          sourceTrackId{ -1 };    // only for TrackLoopback
        bool         armed        { false };
        juce::String clipFilePath;           // path after recording stops
    };

    explicit AudioRecorder(te::Edit& edit, te::Engine& engine);
    ~AudioRecorder();

    // Set the project directory — recordings go into projectDir/samples/
    void setProjectDir(const juce::File& dir) { projectDir = dir; }

    // Device enumeration
    static juce::StringArray listAudioInputs(juce::AudioDeviceManager& dm);

    // Track management
    /** Creates and returns a new audio track (ID = index). */
    int  addAudioTrack(int targetIndex = -1);
    /** Removes an audio track by ID. */
    void removeAudioTrack(int trackId);

    // Source configuration
    void setHardwareInputSource(int trackId, const juce::String& deviceName);
    void setLoopbackSource(int trackId, int sourceTrackId);

    // Recording
    void startRecording(int trackId);
    void stopRecording(int trackId);
    bool isRecording(int trackId) const;

    // Access track infos
    const juce::Array<AudioTrackInfo>& getTrackInfos() const { return trackInfos; }
    AudioTrackInfo* findTrackInfo(int trackId);

    // Called after stop to get the file path written into state
    juce::String getClipFilePath(int trackId) const;

private:
    te::Edit&   edit;
    te::Engine& engine;
    juce::File  projectDir;  // set by SongbirdEditor; recordings go to projectDir/samples/

    juce::Array<AudioTrackInfo> trackInfos;
    juce::CriticalSection       lock;

    // Active recording sessions keyed by trackId
    struct RecordingSession {
        int trackId;
        juce::File outputFile;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
        std::unique_ptr<juce::AudioFormatWriter>                 writer;
    };
    juce::OwnedArray<RecordingSession> activeSessions;

    te::AudioTrack* getAudioTrack(int trackId) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};
