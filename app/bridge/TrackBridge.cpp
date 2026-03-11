#include "SongbirdEditor.h"

//==============================================================================
// Bridge: Audio/MIDI track management, audio device control
//==============================================================================

void SongbirdEditor::registerTrackBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // ====================================================================
        // Audio Tracks
        // ====================================================================

        .withNativeFunction("listAudioInputs", [](auto&, auto complete) {
            auto names = AudioRecorder::listAudioInputs();
            juce::Array<juce::var> arr;
            for (auto& n : names) arr.add(juce::var(n));
            complete(juce::JSON::toString(juce::var(arr)));
        })

        // ====================================================================
        // Audio Device Settings (for Settings panel)
        // ====================================================================

        .withNativeFunction("getAudioDeviceInfo", [this](auto&, auto complete) {
            auto& dm = engine.getDeviceManager().deviceManager;
            auto* device = dm.getCurrentAudioDevice();
            juce::DynamicObject* result = new juce::DynamicObject();
            if (device) {
                result->setProperty("deviceName", device->getName());
                result->setProperty("deviceType", device->getTypeName());
                result->setProperty("sampleRate", device->getCurrentSampleRate());
                result->setProperty("bufferSize", device->getCurrentBufferSizeSamples());
                result->setProperty("inputLatency", device->getInputLatencyInSamples());
                result->setProperty("outputLatency", device->getOutputLatencyInSamples());

                // Available buffer sizes
                juce::Array<juce::var> bufSizes;
                for (auto sz : device->getAvailableBufferSizes())
                    bufSizes.add(juce::var(sz));
                result->setProperty("availableBufferSizes", bufSizes);

                // Available sample rates
                juce::Array<juce::var> sampleRates;
                for (auto sr : device->getAvailableSampleRates())
                    sampleRates.add(juce::var(sr));
                result->setProperty("availableSampleRates", sampleRates);

                // Input channel names
                juce::Array<juce::var> inputs;
                for (auto& n : device->getInputChannelNames())
                    inputs.add(juce::var(n));
                result->setProperty("inputChannels", inputs);

                // Output channel names
                juce::Array<juce::var> outputs;
                for (auto& n : device->getOutputChannelNames())
                    outputs.add(juce::var(n));
                result->setProperty("outputChannels", outputs);
            }

            // Available audio device names (for device switching)
            juce::Array<juce::var> deviceNames;
            for (auto type : dm.getAvailableDeviceTypes()) {
                for (auto& name : type->getDeviceNames(false))  // output devices
                    deviceNames.add(juce::var(name));
            }
            result->setProperty("availableDevices", deviceNames);

            complete(juce::JSON::toString(juce::var(result)));
        })

        .withNativeFunction("listAudioOutputs", [this](auto&, auto complete) {
            auto& dm = engine.getDeviceManager().deviceManager;
            juce::Array<juce::var> arr;
            for (auto type : dm.getAvailableDeviceTypes()) {
                for (auto& name : type->getDeviceNames(false))  // false = output devices
                    arr.add(juce::var(name));
            }
            complete(juce::JSON::toString(juce::var(arr)));
        })

        .withNativeFunction("setAudioDevice", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            juce::String deviceName = args[0].toString();
            auto& dm = engine.getDeviceManager().deviceManager;
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.outputDeviceName = deviceName;
            // Don't blindly set input to the output device name — they may differ
            // (e.g. external interface for output, built-in mic for input)
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioDevice: switched to " + deviceName);
                complete("{\"success\":true}");
            } else {
                DBG("setAudioDevice: error " + err + " — restoring previous device");
                // Restore previous working device so the engine isn't left broken
                auto restoreErr = dm.setAudioDeviceSetup(previousSetup, true);
                if (restoreErr.isNotEmpty()) {
                    // Previous device also gone — try system default
                    DBG("setAudioDevice: restore also failed, trying default device");
                    dm.initialiseWithDefaultDevices(2, 0);
                }
                complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            }
        })

        .withNativeFunction("setAudioSampleRate", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            double sampleRate = static_cast<double>(args[0]);
            auto& dm = engine.getDeviceManager().deviceManager;
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.sampleRate = sampleRate;
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioSampleRate: set to " + juce::String(sampleRate));
                complete("{\"success\":true}");
            } else {
                DBG("setAudioSampleRate: error " + err + " — restoring previous");
                dm.setAudioDeviceSetup(previousSetup, true);
                complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            }
        })

        .withNativeFunction("setAudioBufferSize", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            int bufferSize = static_cast<int>(args[0]);
            auto& dm = engine.getDeviceManager().deviceManager;
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.bufferSize = bufferSize;
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioBufferSize: set to " + juce::String(bufferSize));
                complete("{\"success\":true}");
            } else {
                DBG("setAudioBufferSize: error " + err + " — restoring previous");
                dm.setAudioDeviceSetup(previousSetup, true);
                complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            }
        })

        .withNativeFunction("addAudioTrack", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this, complete = std::move(complete)]() mutable {
                if (!audioRecorder || !edit) { complete("{\"success\":false}"); return; }
                int targetIndex = static_cast<int>(lastParseResult.channels.size());
                int id = audioRecorder->addAudioTrack(targetIndex);
                auto audioTracks = te::getAudioTracks(*edit);
                auto* track = (id >= 0 && id < (int)audioTracks.size()) ? audioTracks[id] : nullptr;
                juce::String name = track ? track->getName() : ("audio" + juce::String(id + 1));
                int vol = track && track->getVolumePlugin()
                    ? static_cast<int>(std::round(juce::Decibels::decibelsToGain(track->getVolumePlugin()->getVolumeDb()) * 127.0))
                    : 80;
                int pan = track && track->getVolumePlugin()
                    ? static_cast<int>(std::round(track->getVolumePlugin()->getPan() * 64.0f))
                    : 0;
                juce::String trackJson = "{\"success\":true,\"trackId\":" + juce::String(id)
                    + ",\"name\":\"" + name.replace("\"", "\\\"") + "\""
                    + ",\"trackType\":\"audio\""
                    + ",\"volume\":" + juce::String(vol)
                    + ",\"pan\":" + juce::String(pan)
                    + "}";
                
                // Register in lastParseResult so it persists
                BirdChannel newCh;
                newCh.channel = id;
                newCh.name = name.toStdString();
                lastParseResult.channels.push_back(newCh);

                if (lastParseResult.arrangement.size() > 0) {
                    // Inject global channel definition into the raw .bird file
                    if (currentBirdFile.existsAsFile()) {
                        auto birdText = currentBirdFile.loadFileAsString();
                        auto lines = juce::StringArray::fromLines(birdText);
                        
                        // Check if it already exists globally
                        juce::String chMarker = "ch " + juce::String(id + 1) + " " + name;
                        bool foundGlobal = false;
                        int insertIdx = lines.size();
                        
                        for (int i = 0; i < lines.size(); ++i) {
                            auto trimmed = lines[i].trim();
                            if (trimmed == chMarker || trimmed.startsWith(chMarker + " ")) {
                                foundGlobal = true;
                                break;
                            }
                            // Stop searching for a global def once we hit arr or sec
                            if (trimmed == "arr" || trimmed.startsWith("sec ")) {
                                insertIdx = i;
                                break;
                            }
                        }

                        if (!foundGlobal) {
                            while (insertIdx > 0 && lines[insertIdx - 1].trim().isEmpty()) {
                                insertIdx--;
                            }
                            lines.insert(insertIdx, chMarker);
                            lines.insert(insertIdx + 1, "  type audio");
                            lines.insert(insertIdx + 2, "  strip console1");
                            currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
                        }
                    }

                    juce::String secNameArg = lastParseResult.arrangement[0].sectionName;
                    int secBars = lastParseResult.arrangement[0].bars;
                    juce::Thread::launch([this, id, secName = juce::String(secNameArg), secBars]() {
                        writeBirdFromClip(id, secName, 0.0, secBars, {});
                    });
                }
                
                commitAndNotify("Add audio track", ProjectState::User);
                
                complete(trackJson);
            });
        })

        .withNativeFunction("addMidiTrack", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this, complete = std::move(complete)]() mutable {
                if (!edit) { complete("{\"success\":false}"); return; }

                int targetIndex = static_cast<int>(lastParseResult.channels.size());
                auto allTracks = te::getAudioTracks(*edit);
                te::Track* preceding = targetIndex > 0 && targetIndex <= (int)allTracks.size() ? allTracks[targetIndex - 1] : nullptr;
                auto track = edit->insertNewAudioTrack(te::TrackInsertPoint(nullptr, preceding), nullptr);
                if (!track) { complete("{\"success\":false}"); return; }

                int id = targetIndex;
                track->setName("track" + juce::String(id + 1));

                // Set default volume (0 dB) and pan (center)
                if (auto vp = track->getVolumePlugin()) {
                    vp->setVolumeDb(0.0f);
                    vp->setPan(0.0f);
                }

                // Add 4 AuxSend plugins (for return bus sends) — matching populateEdit
                for (int bus = 0; bus < 4; bus++) {
                    if (auto plugin = edit->getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {})) {
                        track->pluginList.insertPlugin(*plugin, -1, nullptr);
                        auto* sendPlugin = dynamic_cast<te::AuxSendPlugin*>(plugin.get());
                        if (sendPlugin) {
                            sendPlugin->busNumber = bus;
                            sendPlugin->setGainDb(-100.0f); // Default to muted
                        }
                    }
                }

                // Create an empty MIDI clip spanning the project
                double totalBeats = lastParseResult.bars * 4.0;
                if (totalBeats < 4.0) totalBeats = 16.0;
                auto clipRange = te::TimeRange(
                    edit->tempoSequence.toTime(te::BeatPosition::fromBeats(0.0)),
                    edit->tempoSequence.toTime(te::BeatPosition::fromBeats(totalBeats)));
                track->insertMIDIClip(clipRange, nullptr);

                juce::String trackJson = "{\"success\":true,\"trackId\":" + juce::String(id)
                    + ",\"name\":\"" + track->getName().replace("\"", "\\\"") + "\""
                    + ",\"trackType\":\"midi\""
                    + ",\"volume\":" + juce::String(static_cast<int>(std::round((track->getVolumePlugin() ? juce::Decibels::decibelsToGain(track->getVolumePlugin()->getVolumeDb()) : 0.5) * 127.0)))
                    + ",\"pan\":" + juce::String(static_cast<int>(std::round((track->getVolumePlugin() ? track->getVolumePlugin()->getPan() : 0.0f) * 64.0f)))
                    + "}";

                // Register in lastParseResult so note drawing works
                BirdChannel newCh;
                newCh.channel = id;
                newCh.name = track->getName().toStdString();
                lastParseResult.channels.push_back(newCh);

                if (lastParseResult.arrangement.size() > 0) {
                    // Inject global channel definition into the raw .bird file
                    if (currentBirdFile.existsAsFile()) {
                        auto birdText = currentBirdFile.loadFileAsString();
                        auto lines = juce::StringArray::fromLines(birdText);
                        
                        // Check if it already exists globally
                        juce::String chMarker = "ch " + juce::String(id + 1) + " " + track->getName();
                        bool foundGlobal = false;
                        int insertIdx = lines.size();
                        
                        for (int i = 0; i < lines.size(); ++i) {
                            auto trimmed = lines[i].trim();
                            if (trimmed == chMarker || trimmed.startsWith(chMarker + " ")) {
                                foundGlobal = true;
                                break;
                            }
                            // Stop searching for a global def once we hit arr or sec
                            if (trimmed == "arr" || trimmed.startsWith("sec ")) {
                                insertIdx = i;
                                break;
                            }
                        }

                        if (!foundGlobal) {
                            while (insertIdx > 0 && lines[insertIdx - 1].trim().isEmpty()) {
                                insertIdx--;
                            }
                            lines.insert(insertIdx, chMarker);
                            lines.insert(insertIdx + 1, "  type midi");
                            lines.insert(insertIdx + 2, "  strip console1");
                            currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
                        }
                    }

                    juce::String secNameArg = lastParseResult.arrangement[0].sectionName;
                    int secBars = lastParseResult.arrangement[0].bars;
                    juce::Thread::launch([this, id, secName = juce::String(secNameArg), secBars]() {
                        writeBirdFromClip(id, secName, 0.0, secBars, {});
                    });
                }

                commitAndNotify("Add MIDI track", ProjectState::User);

                complete(trackJson);
            });
        })

        .withNativeFunction("removeAudioTrack", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::MessageManager::callAsync([this, trackId]() {
                audioRecorder->removeAudioTrack(trackId);
                // No trackState emit — JS already filters the track from the store locally
            });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setAudioRecordSource", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::String type = args[1].toString();
            juce::MessageManager::callAsync([this, trackId, type, args, complete = std::move(complete)]() mutable {
                if (type == "hardware")
                    audioRecorder->setHardwareInputSource(trackId, args.size() > 2 ? args[2].toString() : "");
                else if (type == "loopback")
                    audioRecorder->setLoopbackSource(trackId, args.size() > 2 ? (int)args[2] : -1);
                complete("{\"success\":true}");
            });
        })

        .withNativeFunction("setAudioRecordArm", [this](auto& args, auto complete) {
            int  trackId = static_cast<int>(args[0]);
            bool armed   = static_cast<bool>(args[1]);
            juce::MessageManager::callAsync([this, trackId, armed, complete = std::move(complete)]() mutable {
                if (armed) audioRecorder->startRecording(trackId);
                else       audioRecorder->stopRecording(trackId);
                complete("{\"success\":true}");
            });
        });
}
