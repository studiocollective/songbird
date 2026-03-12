#include "SongbirdEditor.h"

//==============================================================================
// Bridge: MIDI/Audio recording, keyboard MIDI, input routing
//==============================================================================

void SongbirdEditor::registerRecordingBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        .withNativeFunction("listMidiInputs", [this](auto& args, auto complete) {
            juce::Array<juce::var> arr;
            
            for (auto& d : juce::MidiInput::getAvailableDevices())
            {
                if (d.name != "Computer Keyboard")
                    arr.add(juce::var(d.name));
            }
            complete(juce::JSON::toString(juce::var(arr)));
        })

        .withNativeFunction("setMidiInputDevice", [this](auto& args, auto complete) {
            // Not used directly when per-track arming is used, but keep the signature
            complete("{\"success\":false}"); 
        })

        // Per-track MIDI input selection (from React RecordStrip)
        // Args: (trackId, inputType)
        //   inputType: "" or "all" = open all devices, "computer-keyboard" = Virtual keyboard,
        //              or a specific device name
        .withNativeFunction("setMidiInput", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            juce::String inputType = args[1].toString();
            
            juce::MessageManager::callAsync([this, trackId, inputType]() {
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size()) return;
                auto* track = audioTracks[trackId];

                juce::String targetDeviceName;
                if (inputType == "computer-keyboard") {
                    targetDeviceName = "Computer Keyboard";
                } else if (!inputType.isEmpty() && inputType != "all") {
                    targetDeviceName = inputType;
                }

                if (targetDeviceName.isNotEmpty() || inputType == "all") {
                    // Enable devices FIRST
                    for (auto& d : engine.getDeviceManager().getMidiInDevices()) {
                        if (inputType == "all" || d->getName() == targetDeviceName) {
                            d->setEnabled(true);
                            d->setMonitorMode(te::InputDevice::MonitorMode::automatic);
                        }
                    }
                }
                
                edit->getTransport().freePlaybackContext();
                edit->getTransport().ensureContextAllocated();

                // Clear existing MIDI assignments for this track
                for (auto* inst : edit->getAllInputDevices()) {
                    if (inst->getInputDevice().isMidi()) {
                        inst->removeTarget(track->itemID, nullptr);
                    }
                }

                if (targetDeviceName.isNotEmpty() || inputType == "all") {
                    // Assign instances using the newly allocated context
                    for (auto* inst : edit->getAllInputDevices()) {
                        if (inst->getInputDevice().isMidi()) {
                            if (inputType == "all" || inst->getInputDevice().getName() == targetDeviceName) {
                                inst->setTarget(track->itemID, true, nullptr);
                            }
                        }
                    }
                }
                
                DBG("setMidiInput: Track " + juce::String(trackId) + " -> " + inputType);
            });
            complete("{\"success\":true}");
        })

        // Toggle input monitoring on a track
        .withNativeFunction("setInputMonitoring", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            bool enabled = static_cast<bool>(args[1]);
            DBG("setInputMonitoring: track " + juce::String(trackId) + " = " + (enabled ? "ON" : "OFF"));
            complete("{\"success\":true}");
        })

        // Send keyboard MIDI note (from computer keyboard used as MIDI input)
        .withNativeFunction("sendKeyboardMidi", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int note     = static_cast<int>(args[0]);
            int velocity = static_cast<int>(args[1]);

            juce::MidiMessage msg = velocity > 0
                ? juce::MidiMessage::noteOn(1, note, (juce::uint8)velocity)
                : juce::MidiMessage::noteOff(1, note);

            juce::MessageManager::callAsync([this, note, velocity, msg]() {
                auto audioTracks = te::getAudioTracks(*edit);
                
                // Only send to armed tracks
                for (int tid : midiArmedTrackIds) {
                    if (tid >= 0 && tid < (int)audioTracks.size()) {
                        audioTracks[tid]->injectLiveMidiMessage(msg, {});
                    }
                }

                // Inject into Tracktion's virtual MIDI devices for recording
                auto& dm = engine.getDeviceManager();
                for (auto& d : dm.getMidiInDevices()) {
                    if (d->getDeviceType() == te::InputDevice::virtualMidiDevice) {
                        if (auto* virt = dynamic_cast<te::VirtualMidiInputDevice*>(d.get())) {
                            virt->handleIncomingMidiMessage(nullptr, msg);
                        }
                    }
                }
            });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setMidiRecordArm", [this](auto& args, auto complete) {
            if (args.size() < 2 || !edit) { complete("{\"success\":false}"); return; }
            int  trackId = static_cast<int>(args[0]);
            bool armed   = static_cast<bool>(args[1]);
            
            juce::MessageManager::callAsync([this, trackId, armed]() {
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size()) { return; }
                auto* track = audioTracks[trackId];
                
                bool recreateContext = false;
                if (armed) {
                    midiArmedTrackIds.insert(trackId);
                    
                    auto instances = edit->getEditInputDevices().getDevicesForTargetTrack(*track);
                    if (instances.isEmpty()) {
                        // Enable Computer Keyboard + All MIDI Inputs virtual devices
                        for (auto& d : engine.getDeviceManager().getMidiInDevices()) {
                            if (d->getName() == "Computer Keyboard" || d->getName() == "All MIDI Inputs") {
                                d->setEnabled(true);
                                d->setMonitorMode(te::InputDevice::MonitorMode::automatic);
                                recreateContext = true;
                            }
                        }
                        // Also enable any physical MIDI devices so they can drive this track
                        for (auto& d : engine.getDeviceManager().getMidiInDevices()) {
                            if (d->getDeviceType() == te::InputDevice::physicalMidiDevice) {
                                d->setEnabled(true);
                                d->setMonitorMode(te::InputDevice::MonitorMode::automatic);
                                recreateContext = true;
                            }
                        }
                    }
                }
                
                if (recreateContext) {
                    edit->getTransport().freePlaybackContext();
                    edit->getTransport().ensureContextAllocated();
                } else {
                    edit->getTransport().ensureContextAllocated();
                }
                
                if (armed) {
                    auto instances = edit->getEditInputDevices().getDevicesForTargetTrack(*track);
                    if (instances.isEmpty()) {
                        // Assign all available MIDI input devices to this track
                        for (auto* inst : edit->getAllInputDevices()) {
                            if (inst->getInputDevice().isMidi()) {
                                inst->setTarget(track->itemID, true, nullptr);
                            }
                        }
                        
                        // Re-query instances
                        instances = edit->getEditInputDevices().getDevicesForTargetTrack(*track);
                    }
                    
                    for (auto* inst : instances) {
                        inst->setRecordingEnabled(track->itemID, true);
                    }
                } else {
                    midiArmedTrackIds.erase(trackId);
                    auto instances = edit->getEditInputDevices().getDevicesForTargetTrack(*track);
                    for (auto* inst : instances) {
                        inst->setRecordingEnabled(track->itemID, false);
                    }
                }
                
            });
            complete("{\"success\":true}");
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
