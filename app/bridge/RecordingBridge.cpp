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
            
            // MIDI routing is handled directly via JUCE MidiInput callbacks
            // (see handleIncomingMidiMessage). Just log the selection here.
            DBG("setMidiInput: Track " + juce::String(trackId) + " -> " + inputType);
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
                
                // Only send to armed tracks for live monitoring
                for (int tid : midiArmedTrackIds) {
                    if (tid >= 0 && tid < (int)audioTracks.size()) {
                        audioTracks[tid]->injectLiveMidiMessage(msg, {});
                    }
                }

                // Also inject into cached virtual MIDI device for recording
                if (cachedVirtualMidiDevice) {
                    cachedVirtualMidiDevice->handleIncomingMidiMessage(nullptr, msg);
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
                
                DBG("setMidiRecordArm: track " + juce::String(trackId) + " armed=" + (armed ? "true" : "false"));
                
                if (armed) {
                    midiArmedTrackIds.insert(trackId);
                    
                    // 1) Open physical MIDI devices via JUCE for live monitoring
                    if (openMidiInputs.empty()) {
                        openAllPhysicalMidiInputs();
                    }
                    
                    // 2) Force Tracktion to rescan MIDI devices for recording
                    engine.getDeviceManager().rescanMidiDeviceList();
                    
                    // 3) Delay to let the 5ms timer fire, then set up Tracktion recording
                    juce::Timer::callAfterDelay(100, [this, trackId]() {
                        setupTracktionRecording(trackId);
                    });
                    
                    DBG("  Armed tracks: " + juce::String((int)midiArmedTrackIds.size()) 
                        + ", Open MIDI inputs: " + juce::String((int)openMidiInputs.size()));
                    
                } else {
                    midiArmedTrackIds.erase(trackId);
                    
                    // Disable recording on this track
                    auto* track = audioTracks[trackId];
                    auto instances = edit->getEditInputDevices().getDevicesForTargetTrack(*track);
                    for (auto* inst : instances) {
                        inst->setRecordingEnabled(track->itemID, false);
                    }
                    
                    DBG("  Disarmed track " + juce::String(trackId) 
                        + ", remaining armed: " + juce::String((int)midiArmedTrackIds.size()));
                    
                    // Close physical MIDI devices when no tracks are armed
                    if (midiArmedTrackIds.empty()) {
                        closeAllPhysicalMidiInputs();
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
