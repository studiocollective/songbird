#include "AudioRecorder.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>

namespace te = tracktion;

AudioRecorder::AudioRecorder(te::Edit& edit, te::Engine& engine)
    : edit(edit), engine(engine)
{
}

AudioRecorder::~AudioRecorder()
{
    // Stop any outstanding recordings
    for (auto* session : activeSessions)
        stopRecording(session->trackId);
}

//==============================================================================
// Device enumeration
//==============================================================================

juce::StringArray AudioRecorder::listAudioInputs()
{
    juce::StringArray names;

    // Use JUCE's audio device manager to query hardware input channels
    juce::AudioDeviceManager tempDM;
    tempDM.initialiseWithDefaultDevices(2, 2); // 2 in, 2 out

    if (auto* device = tempDM.getCurrentAudioDevice())
    {
        auto inputNames = device->getInputChannelNames();
        for (auto& name : inputNames)
            names.add(name);
    }

    // Fallback: list raw device type names
    if (names.isEmpty())
    {
        for (auto type : tempDM.getAvailableDeviceTypes())
        {
            for (auto& name : type->getDeviceNames(true))
                names.add(name);
        }
    }

    return names;
}

//==============================================================================
// Track management
//==============================================================================

int AudioRecorder::addAudioTrack(int targetIndex)
{
    auto allTracks = te::getAudioTracks(edit);
    if (targetIndex < 0 || targetIndex > (int)allTracks.size())
        targetIndex = static_cast<int>(allTracks.size());

    te::Track* preceding = targetIndex > 0 ? allTracks[targetIndex - 1] : nullptr;
    auto newTrack = edit.insertNewAudioTrack(te::TrackInsertPoint(nullptr, preceding), nullptr);
    if (!newTrack) return -1;

    newTrack->setName("audio" + juce::String(targetIndex + 1));

    AudioTrackInfo info;
    info.trackId   = targetIndex;
    info.sourceType = SourceType::HardwareInput;
    info.sourceName = "No Input";

    juce::ScopedLock sl(lock);
    trackInfos.add(info);

    DBG("AudioRecorder: Added audio track " + juce::String(targetIndex));
    return targetIndex;
}

void AudioRecorder::removeAudioTrack(int trackId)
{
    // Stop recording if active
    stopRecording(trackId);

    auto* track = getAudioTrack(trackId);
    if (track)
        edit.deleteTrack(track);

    juce::ScopedLock sl(lock);
    for (int i = trackInfos.size() - 1; i >= 0; --i)
        if (trackInfos[i].trackId == trackId)
        { trackInfos.remove(i); break; }

    DBG("AudioRecorder: Removed audio track " + juce::String(trackId));
}

//==============================================================================
// Source configuration
//==============================================================================

void AudioRecorder::setHardwareInputSource(int trackId, const juce::String& deviceName)
{
    juce::ScopedLock sl(lock);
    if (auto* info = findTrackInfo(trackId))
    {
        info->sourceType   = SourceType::HardwareInput;
        info->sourceName   = deviceName;
        info->sourceTrackId = -1;
        DBG("AudioRecorder: Track " + juce::String(trackId) + " src → hardware '" + deviceName + "'");
    }
}

void AudioRecorder::setLoopbackSource(int trackId, int sourceTrackId)
{
    juce::ScopedLock sl(lock);
    if (auto* info = findTrackInfo(trackId))
    {
        // Wire an AuxSend on the source track → AuxReturn on bus 3 (reserved for loopback)
        if (auto* srcTrack = getAudioTrack(sourceTrackId))
        {
            // Ensure source track has an AuxSend on bus 3
            bool foundSend = false;
            for (auto p : srcTrack->pluginList)
                if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p))
                    if (send->busNumber == 3) { foundSend = true; break; }

            if (!foundSend)
            {
                if (auto plugin = edit.getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {}))
                {
                    srcTrack->pluginList.insertPlugin(*plugin, -1, nullptr);
                    if (auto* sp = dynamic_cast<te::AuxSendPlugin*>(plugin.get()))
                    {
                        sp->busNumber = 3;
                        sp->setGainDb(0.0f);
                    }
                }
            }
        }

        info->sourceType    = SourceType::TrackLoopback;
        info->sourceTrackId = sourceTrackId;
        info->sourceName    = "Loopback";
        DBG("AudioRecorder: Track " + juce::String(trackId) + " src → loopback from track " + juce::String(sourceTrackId));
    }
}

//==============================================================================
// Recording
//==============================================================================

void AudioRecorder::startRecording(int trackId)
{
    auto* info = findTrackInfo(trackId);
    if (!info) { DBG("AudioRecorder: startRecording — track " + juce::String(trackId) + " not found"); return; }
    if (isRecording(trackId)) { DBG("AudioRecorder: already recording track " + juce::String(trackId)); return; }

    // Build output file path inside the project's samples folder
    juce::File samplesDir;
    if (projectDir != juce::File())
        samplesDir = projectDir.getChildFile("samples");
    else
        samplesDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("songbird_recordings");
    samplesDir.createDirectory();

    auto timestamp  = juce::String(juce::Time::currentTimeMillis());
    auto outputFile = samplesDir.getChildFile("track" + juce::String(trackId) + "_" + timestamp + ".wav");

    // Create a WAV writer
    juce::WavAudioFormat wavFormat;
    auto stream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
    if (!stream) { DBG("AudioRecorder: Could not open output file"); return; }

    // Use the edit's sample rate / stereo
    double sampleRate = 44100.0;
    auto& dm = engine.getDeviceManager().deviceManager;
    if (auto* dev = dm.getCurrentAudioDevice())
        sampleRate = dev->getCurrentSampleRate();

    auto* writer = wavFormat.createWriterFor(stream.release(), sampleRate, 2, 24, {}, 0);
    if (!writer) { DBG("AudioRecorder: Could not create WAV writer"); return; }

    auto* session  = new RecordingSession();
    session->trackId    = trackId;
    session->outputFile = outputFile;
    session->writer.reset(writer);

    {
        juce::ScopedLock sl(lock);
        activeSessions.add(session);
        info->armed = true;
    }

    DBG("AudioRecorder: Started recording track " + juce::String(trackId) + " → " + outputFile.getFullPathName());
}

void AudioRecorder::stopRecording(int trackId)
{
    RecordingSession* session = nullptr;
    int sessionIdx = -1;

    {
        juce::ScopedLock sl(lock);
        for (int i = 0; i < activeSessions.size(); ++i)
        {
            if (activeSessions[i]->trackId == trackId)
            {
                session    = activeSessions[i];
                sessionIdx = i;
                break;
            }
        }
    }

    if (!session) return;

    // Flush and close writer
    session->writer.reset();
    session->threadedWriter.reset();

    juce::File clipFile = session->outputFile;

    {
        juce::ScopedLock sl(lock);
        activeSessions.remove(sessionIdx);

        if (auto* info = findTrackInfo(trackId))
        {
            info->armed        = false;
            info->clipFilePath = clipFile.getFullPathName();
        }
    }

    // Insert a WaveAudioClip on the track pointing to the recorded file
    juce::MessageManager::callAsync([this, trackId, clipFile]()
    {
        auto* track = getAudioTrack(trackId);
        if (!track) return;

        auto& transport = edit.getTransport();
        auto endPos = te::toTime(transport.getLoopRange().getEnd(), edit.tempoSequence);
        te::ClipPosition clipPos { te::TimeRange(te::TimePosition(), endPos) };

        if (auto clip = track->insertWaveClip(clipFile.getFileNameWithoutExtension(),
                                              clipFile,
                                              clipPos,
                                              false))
        {
            DBG("AudioRecorder: Inserted WaveAudioClip on track " + juce::String(trackId));
        }
    });

    DBG("AudioRecorder: Stopped recording track " + juce::String(trackId));
}

bool AudioRecorder::isRecording(int trackId) const
{
    for (auto* session : activeSessions)
        if (session->trackId == trackId) return true;
    return false;
}

juce::String AudioRecorder::getClipFilePath(int trackId) const
{
    for (auto& info : trackInfos)
        if (info.trackId == trackId) return info.clipFilePath;
    return {};
}

//==============================================================================
// Helpers
//==============================================================================

AudioRecorder::AudioTrackInfo* AudioRecorder::findTrackInfo(int trackId)
{
    for (auto& info : trackInfos)
        if (info.trackId == trackId) return &info;
    return nullptr;
}

te::AudioTrack* AudioRecorder::getAudioTrack(int trackId) const
{
    auto tracks = te::getAudioTracks(edit);
    if (trackId >= 0 && trackId < (int)tracks.size())
        return tracks[trackId];
    return nullptr;
}
