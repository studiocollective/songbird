#include "MidiRecorder.h"
#include <algorithm>

namespace {
    // Convert transport time → beats using the Edit's tempo sequence
    static double timeToBeats(te::Edit& edit, double timeInSeconds)
    {
        auto t = te::TimePosition::fromSeconds(timeInSeconds);
        return edit.tempoSequence.toBeats(t).inBeats();
    }
}

MidiRecorder::MidiRecorder(te::Edit& edit)
    : edit(edit)
{
}

MidiRecorder::~MidiRecorder()
{
    stopRecording();
    closeDevice();
}

//==============================================================================
// Device management
//==============================================================================

juce::StringArray MidiRecorder::listMidiDevices()
{
    juce::StringArray names;
    for (auto& info : juce::MidiInput::getAvailableDevices())
        names.add(info.name);
    return names;
}

bool MidiRecorder::openDevice(const juce::String& deviceName)
{
    closeDevice(); // close any previously open device

    for (auto& info : juce::MidiInput::getAvailableDevices())
    {
        if (info.name == deviceName)
        {
            midiInput = juce::MidiInput::openDevice(info.identifier, this);
            if (midiInput)
            {
                midiInput->start();
                openDeviceName = deviceName;
                DBG("MidiRecorder: Opened '" + deviceName + "'");
                return true;
            }
        }
    }

    DBG("MidiRecorder: Could not open device '" + deviceName + "'");
    return false;
}

void MidiRecorder::closeDevice()
{
    if (midiInput)
    {
        midiInput->stop();
        midiInput.reset();
        openDeviceName.clear();
        DBG("MidiRecorder: Device closed");
    }
}

//==============================================================================
// Recording
//==============================================================================

void MidiRecorder::startRecording(te::AudioTrack* track, double startBeatPosition)
{
    juce::ScopedLock sl(pendingLock);

    recordingTrack  = track;
    recordStartBeat = startBeatPosition;
    pendingNotes.clear();

    {
        juce::ScopedLock rl(recordedLock);
        recordedMessages.clear();
    }

    recording.store(true);
    DBG("MidiRecorder: Recording started on track '" +
        (track ? track->getName() : "<null>") + "'");
}

void MidiRecorder::stopRecording()
{
    if (!recording.exchange(false))
        return; // already stopped

    // Close any pending note-ons (no matching note-off received yet)
    {
        juce::ScopedLock sl(pendingLock);
        double endBeat = getCurrentBeat();
        for (auto& pn : pendingNotes)
        {
            double duration = std::max(0.01, endBeat - pn.startBeat);
            if (recordingTrack)
            {
                // Find or create a MidiClip covering at least this range
                auto clips = recordingTrack->getClips();
                te::MidiClip* midiClip = nullptr;
                for (auto* clip : clips)
                    if (auto* mc = dynamic_cast<te::MidiClip*>(clip))
                    { midiClip = mc; break; }

                if (midiClip)
                {
                    midiClip->getSequence().addNote(
                        pn.pitch,
                        te::BeatPosition::fromBeats(pn.startBeat),
                        te::BeatDuration::fromBeats(duration),
                        pn.velocity,
                        0, nullptr);
                }
            }
        }
        pendingNotes.clear();
    }

    recordingTrack = nullptr;
    DBG("MidiRecorder: Recording stopped, " +
        juce::String(recordedMessages.size()) + " messages captured");
}

//==============================================================================
// MIDI callback (called from a high-priority audio thread)
//==============================================================================

void MidiRecorder::handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                              const juce::MidiMessage& msg)
{
    if (!recording.load())
        return;

    // Ignore non-note messages (for now — CC/pitch bend could be added later)
    if (!msg.isNoteOn() && !msg.isNoteOff())
        return;

    double beatNow = getCurrentBeat();

    // Store into serialisable log
    {
        juce::ScopedLock rl(recordedLock);
        TimedMidiMessage tm;
        tm.beatPosition = beatNow;
        tm.message = msg;
        recordedMessages.add(tm);
    }

    if (msg.isNoteOn() && msg.getVelocity() > 0)
    {
        juce::ScopedLock sl(pendingLock);
        PendingNote pn;
        pn.startBeat = beatNow;
        pn.pitch     = msg.getNoteNumber();
        pn.velocity  = msg.getVelocity();
        pendingNotes.add(pn);
    }
    else // note-off
    {
        juce::ScopedLock sl(pendingLock);
        int pitch = msg.getNoteNumber();
        for (int i = pendingNotes.size() - 1; i >= 0; --i)
        {
            if (pendingNotes[i].pitch == pitch)
            {
                auto& pn = pendingNotes.getReference(i);
                double duration = std::max(0.01, beatNow - pn.startBeat);

                // Write directly into the edit's MidiClip on the message thread
                // (we schedule it async to stay off the audio thread)
                auto startBeat = pn.startBeat;
                auto vel       = pn.velocity;
                auto trackPtr  = recordingTrack;

                juce::MessageManager::callAsync([this, trackPtr, pitch, startBeat, duration, vel]() {
                    if (!trackPtr) return;

                    te::MidiClip* midiClip = nullptr;
                    for (auto* clip : trackPtr->getClips())
                        if (auto* mc = dynamic_cast<te::MidiClip*>(clip))
                        { midiClip = mc; break; }

                    if (midiClip)
                    {
                        midiClip->getSequence().addNote(
                            pitch,
                            te::BeatPosition::fromBeats(startBeat),
                            te::BeatDuration::fromBeats(duration),
                            vel, 0, nullptr);
                    }

                    if (onNoteRecorded) onNoteRecorded();
                });

                pendingNotes.remove(i);
                break;
            }
        }
    }
}

double MidiRecorder::getCurrentBeat() const
{
    auto& transport = edit.getTransport();
    double sec = transport.getPosition().inSeconds();
    return timeToBeats(const_cast<te::Edit&>(edit), sec);
}
