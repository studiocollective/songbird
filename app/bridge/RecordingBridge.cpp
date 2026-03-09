#include "SongbirdEditor.h"

//==============================================================================
// Bridge: MIDI/Audio recording, keyboard MIDI, input routing
//==============================================================================

void SongbirdEditor::registerRecordingBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        .withNativeFunction("listMidiInputs", [this](auto&, auto complete) {
            auto devices = MidiRecorder::listMidiDevices();
            juce::Array<juce::var> arr;
            for (auto& d : devices)
                arr.add(juce::var(d));
            complete(juce::JSON::toString(juce::var(arr)));
        })

        .withNativeFunction("setMidiInputDevice", [this](auto& args, auto complete) {
            if (args.size() > 0 && midiRecorder) {
                juce::String name = args[0].toString();
                bool ok = midiRecorder->openDevice(name);
                complete(ok ? "{\"success\":true}" : "{\"success\":false}");
            } else complete("{\"success\":false}");
        })

        // Per-track MIDI input selection (from React RecordStrip)
        // Args: (trackId, inputType)
        //   inputType: "" or "all" = open all devices, "computer-keyboard" = close hardware,
        //              or a specific device name
        .withNativeFunction("setMidiInput", [this](auto& args, auto complete) {
            if (args.size() < 2 || !midiRecorder) { complete("{\"success\":false}"); return; }
            juce::String inputType = args[1].toString();
            juce::MessageManager::callAsync([this, inputType]() {
                if (inputType.isEmpty() || inputType == "all") {
                    // Open first available MIDI device (or keep current)
                    auto devices = MidiRecorder::listMidiDevices();
                    if (devices.size() > 0 && midiRecorder->getOpenDeviceName().isEmpty())
                        midiRecorder->openDevice(devices[0]);
                    DBG("setMidiInput: All Inputs");
                } else if (inputType == "computer-keyboard") {
                    // Close hardware device — keyboard MIDI is injected via sendKeyboardMidi
                    midiRecorder->closeDevice();
                    DBG("setMidiInput: Computer Keyboard");
                } else {
                    // Open specific device
                    midiRecorder->openDevice(inputType);
                    DBG("setMidiInput: " + inputType);
                }
            });
            complete("{\"success\":true}");
        })

        // Toggle input monitoring on a track
        // Args: (trackId, enabled)
        // Note: actual audio monitoring pass-through requires AudioRecorder support;
        // this stores the intent and logs it for now.
        .withNativeFunction("setInputMonitoring", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            bool enabled = static_cast<bool>(args[1]);
            DBG("setInputMonitoring: track " + juce::String(trackId) + " = " + (enabled ? "ON" : "OFF"));
            // TODO: Wire up audio pass-through via AudioRecorder for live monitoring
            complete("{\"success\":true}");
        })

        // Send keyboard MIDI note (from computer keyboard used as MIDI input)
        // Args: (noteNumber, velocity)
        //   velocity > 0 = note-on, velocity == 0 = note-off
        .withNativeFunction("sendKeyboardMidi", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int note     = static_cast<int>(args[0]);
            int velocity = static_cast<int>(args[1]);

            juce::MessageManager::callAsync([this, note, velocity]() {
                // Find the armed MIDI track and inject the note into it
                auto audioTracks = te::getAudioTracks(*edit);
                te::AudioTrack* targetTrack = nullptr;

                // If we have an armed MIDI recording track, use that
                if (midiRecordTrackId >= 0 && midiRecordTrackId < (int)audioTracks.size())
                    targetTrack = audioTracks[midiRecordTrackId];

                if (!targetTrack) {
                    // Fall back: find the first track with a loaded instrument plugin
                    for (auto* track : audioTracks) {
                        for (auto* plugin : track->pluginList) {
                            if (dynamic_cast<te::ExternalPlugin*>(plugin)) {
                                targetTrack = track;
                                break;
                            }
                        }
                        if (targetTrack) break;
                    }
                }

                if (targetTrack) {
                    juce::MidiMessage msg = velocity > 0
                        ? juce::MidiMessage::noteOn(1, note, (juce::uint8)velocity)
                        : juce::MidiMessage::noteOff(1, note);

                    // Inject into Tracktion's audio graph — this flows through
                    // the track's plugin chain and produces audible output
                    targetTrack->injectLiveMidiMessage(msg, {});

                    // If recording is armed, also feed to MidiRecorder
                    if (midiRecorder && midiRecorder->isRecording()) {
                        midiRecorder->handleIncomingMidiMessage(nullptr, msg);
                    }

                    DBG("sendKeyboardMidi: note=" + juce::String(note) + " vel=" + juce::String(velocity)
                        + " -> track " + targetTrack->getName());
                }
            });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setMidiRecordArm", [this](auto& args, auto complete) {
            { complete("{\"success\":false}"); return; }
            int  trackId = static_cast<int>(args[0]);
            bool armed   = static_cast<bool>(args[1]);
            juce::MessageManager::callAsync([this, trackId, armed, complete = std::move(complete)]() mutable {
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size())
                { complete("{\"success\":false}"); return; }
                auto* track = audioTracks[trackId];
                if (armed) {
                    double beatNow = edit->tempoSequence.toBeats(edit->getTransport().getPosition()).inBeats();
                    midiRecorder->startRecording(track, beatNow);
                    midiRecordTrackId = trackId;
                } else {
                    midiRecorder->stopRecording();
                    midiRecordTrackId = -1;
                }
                complete("{\"success\":true}");
            });
        })

        .withNativeFunction("clearRecordedMidi", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::MessageManager::callAsync([this, trackId]() {
                auto tracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)tracks.size()) {
                    for (auto* clip : tracks[trackId]->getClips())
                        clip->state.getParent().removeChild(clip->state, nullptr);
                    BirdLoader::populateEdit(*edit, lastParseResult, engine);
                    // Emit lightweight notesChanged for just this track (not full trackState)
                    if (webView) {
                        juce::String notesJson = "{\"trackId\":" + juce::String(trackId) + ",\"notes\":[";
                        te::MidiClip* mc = nullptr;
                        for (auto* clip : tracks[trackId]->getClips())
                            if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                        if (mc) {
                            auto& seq = mc->getSequence();
                            for (int n = 0; n < seq.getNumNotes(); n++) {
                                auto* note = seq.getNote(n);
                                if (n > 0) notesJson += ",";
                                notesJson += "{\"pitch\":" + juce::String(note->getNoteNumber()) +
                                             ",\"beat\":" + juce::String(note->getStartBeat().inBeats(), 3) +
                                             ",\"duration\":" + juce::String(note->getLengthBeats().inBeats(), 3) +
                                             ",\"velocity\":" + juce::String(note->getVelocity()) + "}";
                            }
                        }
                        notesJson += "]}";
                        webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesJson));
                    }
                }
            });
            complete("{\"success\":true}");
        });
}
