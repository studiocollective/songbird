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

        .withNativeFunction("listAudioInputs", [this](auto&, auto complete) {
            auto& dm = engine.getDeviceManager().deviceManager;
            auto names = AudioRecorder::listAudioInputs(dm);
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
            auto currentSetup = dm.getAudioDeviceSetup();
            juce::DynamicObject* result = new juce::DynamicObject();
            if (device) {
                result->setProperty("deviceName", device->getName());
                result->setProperty("deviceType", device->getTypeName());
                result->setProperty("sampleRate", device->getCurrentSampleRate());
                result->setProperty("bufferSize", device->getCurrentBufferSizeSamples());
                result->setProperty("inputLatency", device->getInputLatencyInSamples());
                result->setProperty("outputLatency", device->getOutputLatencyInSamples());

                DBG("AudioDeviceInfo: sr=" + juce::String(device->getCurrentSampleRate())
                    + " buf=" + juce::String(device->getCurrentBufferSizeSamples())
                    + " inLat=" + juce::String(device->getInputLatencyInSamples())
                    + " outLat=" + juce::String(device->getOutputLatencyInSamples())
                    + " inDev=" + currentSetup.inputDeviceName
                    + " outDev=" + currentSetup.outputDeviceName);

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

            // Current input/output device names from setup
            result->setProperty("inputDeviceName", currentSetup.inputDeviceName);
            result->setProperty("outputDeviceName", currentSetup.outputDeviceName);

            // Available output devices
            juce::Array<juce::var> outputDeviceNames;
            for (auto type : dm.getAvailableDeviceTypes()) {
                for (auto& name : type->getDeviceNames(false))  // false = output devices
                    outputDeviceNames.add(juce::var(name));
            }
            result->setProperty("availableOutputDevices", outputDeviceNames);

            // Available input devices
            juce::Array<juce::var> inputDeviceNames;
            for (auto type : dm.getAvailableDeviceTypes()) {
                for (auto& name : type->getDeviceNames(true))  // true = input devices
                    inputDeviceNames.add(juce::var(name));
            }
            result->setProperty("availableInputDevices", inputDeviceNames);

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

        .withNativeFunction("setAudioOutputDevice", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            juce::String deviceName = args[0].toString();
            auto& dm = engine.getDeviceManager().deviceManager;
            // Stop transport before device change to avoid transients
            bool wasPlaying = edit && edit->getTransport().isPlaying();
            double pos = edit ? edit->getTransport().getPosition().inSeconds() : 0.0;
            if (wasPlaying) edit->getTransport().stop(false, false);
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.outputDeviceName = deviceName;
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioOutputDevice: switched to " + deviceName);
                if (wasPlaying && edit) {
                    edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                    edit->getTransport().play(false);
                }
                complete("{\"success\":true}");
            } else {
                DBG("setAudioOutputDevice: error " + err + " — restoring previous device");
                auto restoreErr = dm.setAudioDeviceSetup(previousSetup, true);
                if (restoreErr.isNotEmpty()) {
                    DBG("setAudioOutputDevice: restore also failed, trying default device");
                    dm.initialiseWithDefaultDevices(2, 0);
                }
                if (wasPlaying && edit) {
                    edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                    edit->getTransport().play(false);
                }
                complete("{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
            }
        })

        .withNativeFunction("setAudioInputDevice", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            juce::String deviceName = args[0].toString();
            auto& dm = engine.getDeviceManager().deviceManager;
            bool wasPlaying = edit && edit->getTransport().isPlaying();
            double pos = edit ? edit->getTransport().getPosition().inSeconds() : 0.0;
            if (wasPlaying) edit->getTransport().stop(false, false);
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.inputDeviceName = deviceName;  // empty string = no input
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioInputDevice: switched to " + (deviceName.isEmpty() ? "None" : deviceName));
            } else {
                DBG("setAudioInputDevice: error " + err + " — restoring previous device");
                dm.setAudioDeviceSetup(previousSetup, true);
            }
            if (wasPlaying && edit) {
                edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                edit->getTransport().play(false);
            }
            complete(err.isEmpty() ? "{\"success\":true}" : "{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
        })

        .withNativeFunction("setAudioSampleRate", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            double sampleRate = static_cast<double>(args[0]);
            auto& dm = engine.getDeviceManager().deviceManager;
            bool wasPlaying = edit && edit->getTransport().isPlaying();
            double pos = edit ? edit->getTransport().getPosition().inSeconds() : 0.0;
            if (wasPlaying) edit->getTransport().stop(false, false);
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.sampleRate = sampleRate;
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioSampleRate: set to " + juce::String(sampleRate));
            } else {
                DBG("setAudioSampleRate: error " + err + " — restoring previous");
                dm.setAudioDeviceSetup(previousSetup, true);
            }
            if (wasPlaying && edit) {
                edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                edit->getTransport().play(false);
            }
            complete(err.isEmpty() ? "{\"success\":true}" : "{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
        })

        .withNativeFunction("setAudioBufferSize", [this](auto& args, auto complete) {
            if (args.size() < 1) { complete("{\"success\":false}"); return; }
            int bufferSize = static_cast<int>(args[0]);
            auto& dm = engine.getDeviceManager().deviceManager;
            bool wasPlaying = edit && edit->getTransport().isPlaying();
            double pos = edit ? edit->getTransport().getPosition().inSeconds() : 0.0;
            if (wasPlaying) edit->getTransport().stop(false, false);
            auto previousSetup = dm.getAudioDeviceSetup();
            auto setup = previousSetup;
            setup.bufferSize = bufferSize;
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty()) {
                DBG("setAudioBufferSize: set to " + juce::String(bufferSize));
            } else {
                DBG("setAudioBufferSize: error " + err + " — restoring previous");
                dm.setAudioDeviceSetup(previousSetup, true);
            }
            if (wasPlaying && edit) {
                edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                edit->getTransport().play(false);
            }
            complete(err.isEmpty() ? "{\"success\":true}" : "{\"success\":false,\"error\":\"" + err.replace("\"", "\\\"") + "\"}");
        })

        .withNativeFunction("addAudioTrack", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this, complete = std::move(complete)]() mutable {
                if (!edit) { complete("{\"success\":false}"); return; }

                int targetIndex = static_cast<int>(lastParseResult.channels.size());
                auto allTracks = te::getAudioTracks(*edit);
                te::Track* preceding = targetIndex > 0 && targetIndex <= (int)allTracks.size() ? allTracks[targetIndex - 1] : nullptr;
                auto track = edit->insertNewAudioTrack(te::TrackInsertPoint(nullptr, preceding), nullptr);
                if (!track) { complete("{\"success\":false}"); return; }

                int id = targetIndex;
                track->setName("audio" + juce::String(id + 1));

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

                int vol = track->getVolumePlugin()
                    ? static_cast<int>(std::round(juce::Decibels::decibelsToGain(track->getVolumePlugin()->getVolumeDb()) * 127.0))
                    : 80;
                int pan = track->getVolumePlugin()
                    ? static_cast<int>(std::round(track->getVolumePlugin()->getPan() * 64.0f))
                    : 0;
                juce::String name = track->getName();
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
                newCh.trackType = "audio";
                lastParseResult.channels.push_back(newCh);

                if (lastParseResult.arrangement.size() > 0) {
                    // Inject channel into the channels block in the .bird file
                    if (currentBirdFile.existsAsFile()) {
                        auto birdText = currentBirdFile.loadFileAsString();
                        auto lines = juce::StringArray::fromLines(birdText);

                        // Find the channels block
                        int channelsBlockStart = -1;
                        int channelsBlockEnd = -1;
                        for (int i = 0; i < lines.size(); ++i) {
                            if (lines[i].trim() == "channels") {
                                channelsBlockStart = i;
                                // Find end: next non-indented non-empty line
                                for (int j = i + 1; j < lines.size(); ++j) {
                                    auto t = lines[j].trim();
                                    if (t.isEmpty()) continue;
                                    if (!lines[j].startsWithChar(' ') && !lines[j].startsWithChar('\t')) {
                                        channelsBlockEnd = j;
                                        break;
                                    }
                                }
                                if (channelsBlockEnd < 0) channelsBlockEnd = lines.size();
                                // Walk back past trailing blank lines so new track appears
                                // right after the last existing channel, not after whitespace
                                while (channelsBlockEnd > channelsBlockStart + 1 && lines[channelsBlockEnd - 1].trim().isEmpty())
                                    channelsBlockEnd--;
                                break;
                            }
                        }

                        if (channelsBlockStart >= 0) {
                            // Insert at end of channels block
                            lines.insert(channelsBlockEnd, "  " + juce::String(id + 1) + " " + name);
                            lines.insert(channelsBlockEnd + 1, "    type audio");
                            lines.insert(channelsBlockEnd + 2, "    strip console1");
                        } else {
                            // No channels block — create one before arr/sec
                            int insertIdx = lines.size();
                            for (int i = 0; i < lines.size(); ++i) {
                                auto trimmed = lines[i].trim();
                                if (trimmed == "arr" || trimmed.startsWith("sec ")) {
                                    insertIdx = i;
                                    break;
                                }
                            }
                            while (insertIdx > 0 && lines[insertIdx - 1].trim().isEmpty())
                                insertIdx--;
                            lines.insert(insertIdx, "channels");
                            lines.insert(insertIdx + 1, "  " + juce::String(id + 1) + " " + name);
                            lines.insert(insertIdx + 2, "    type audio");
                            lines.insert(insertIdx + 3, "    strip console1");
                        }
                        currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
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
                    // Inject channel into the channels block in the .bird file
                    if (currentBirdFile.existsAsFile()) {
                        auto birdText = currentBirdFile.loadFileAsString();
                        auto lines = juce::StringArray::fromLines(birdText);

                        // Find the channels block
                        int channelsBlockStart = -1;
                        int channelsBlockEnd = -1;
                        for (int i = 0; i < lines.size(); ++i) {
                            if (lines[i].trim() == "channels") {
                                channelsBlockStart = i;
                                for (int j = i + 1; j < lines.size(); ++j) {
                                    auto t = lines[j].trim();
                                    if (t.isEmpty()) continue;
                                    if (!lines[j].startsWithChar(' ') && !lines[j].startsWithChar('\t')) {
                                        channelsBlockEnd = j;
                                        break;
                                    }
                                }
                                if (channelsBlockEnd < 0) channelsBlockEnd = lines.size();
                                // Walk back past trailing blank lines so new track appears
                                // right after the last existing channel, not after whitespace
                                while (channelsBlockEnd > channelsBlockStart + 1 && lines[channelsBlockEnd - 1].trim().isEmpty())
                                    channelsBlockEnd--;
                                break;
                            }
                        }

                        if (channelsBlockStart >= 0) {
                            lines.insert(channelsBlockEnd, "  " + juce::String(id + 1) + " " + track->getName());
                            lines.insert(channelsBlockEnd + 1, "    type midi");
                            lines.insert(channelsBlockEnd + 2, "    strip console1");
                        } else {
                            int insertIdx = lines.size();
                            for (int i = 0; i < lines.size(); ++i) {
                                auto trimmed = lines[i].trim();
                                if (trimmed == "arr" || trimmed.startsWith("sec ")) {
                                    insertIdx = i;
                                    break;
                                }
                            }
                            while (insertIdx > 0 && lines[insertIdx - 1].trim().isEmpty())
                                insertIdx--;
                            lines.insert(insertIdx, "channels");
                            lines.insert(insertIdx + 1, "  " + juce::String(id + 1) + " " + track->getName());
                            lines.insert(insertIdx + 2, "    type midi");
                            lines.insert(insertIdx + 3, "    strip console1");
                        }
                        currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
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

        .withNativeFunction("removeTrack", [this](auto& args, auto complete) {
            int trackId = static_cast<int>(args[0]);
            juce::MessageManager::callAsync([this, trackId, complete = std::move(complete)]() mutable {
                if (!edit) { complete("{\"success\":false}"); return; }
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size()) {
                    complete("{\"success\":false}"); return;
                }
                auto* track = audioTracks[trackId];
                if (!track) { complete("{\"success\":false}"); return; }

                // Capture the track name before deletion for bird file cleanup
                juce::String trackName = track->getName();

                edit->deleteTrack(track);

                // Remove from lastParseResult.channels
                if (trackId < (int)lastParseResult.channels.size())
                    lastParseResult.channels.erase(lastParseResult.channels.begin() + trackId);

                // Remove from bird file (channels block + section entries)
                if (currentBirdFile.existsAsFile()) {
                    auto birdText = currentBirdFile.loadFileAsString();
                    auto lines = juce::StringArray::fromLines(birdText);
                    juce::StringArray newLines;

                    bool skipping = false;
                    int skipIndent = 0; // indentation level of the matched line

                    for (int i = 0; i < lines.size(); ++i) {
                        auto trimmed = lines[i].trim();

                        // Compute indentation of this line
                        int indent = 0;
                        for (auto c : lines[i].toStdString()) {
                            if (c == ' ') indent++;
                            else if (c == '\t') indent += 2;
                            else break;
                        }

                        // Match channel in channels block: "  N trackName"
                        // Match channel in section: "  ch N trackName"
                        bool isChannelMatch = false;
                        if (!trimmed.isEmpty()) {
                            auto tokens = juce::StringArray::fromTokens(trimmed, " ", "");
                            if (tokens.size() >= 2) {
                                // channels block format: "N name"
                                bool firstIsDigit = tokens[0].containsOnly("0123456789");
                                if (firstIsDigit && tokens[1] == trackName)
                                    isChannelMatch = true;
                                // section format: "ch N name"
                                if (tokens[0] == "ch" && tokens.size() >= 3 && tokens[2] == trackName)
                                    isChannelMatch = true;
                                // backward compat: bare "ch N name" at top level
                                if (tokens[0] == "ch" && tokens.size() >= 3 && tokens[2] == trackName && indent == 0)
                                    isChannelMatch = true;
                            }
                        }

                        if (isChannelMatch) {
                            skipping = true;
                            skipIndent = indent;
                            continue;
                        }

                        if (skipping) {
                            if (trimmed.isEmpty()) continue; // skip blank lines within block
                            // Stop skipping when indentation is <= the matched line
                            if (indent <= skipIndent) {
                                skipping = false;
                            } else {
                                continue; // deeper indentation = part of the channel
                            }
                        }

                        newLines.add(lines[i]);
                    }

                    currentBirdFile.replaceWithText(newLines.joinIntoString("\n"));
                }

                // Persist state + re-emit updated track list
                saveEditState();
                createTrackWatchers();

                // Re-emit trackState so JS gets the updated track list
                if (webView) {
                    auto fullJsonStr = getTrackStateJSON();
                    auto parsed = juce::JSON::parse(fullJsonStr);
                    if (auto* tracks = parsed.getProperty("tracks", {}).getArray()) {
                        for (auto& t : *tracks) {
                            if (auto* obj = t.getDynamicObject())
                                obj->setProperty("notes", juce::Array<juce::var>());
                        }
                    }
                    webView->emitEventIfBrowserIsVisible("trackState", parsed);
                }

                saveStateCache();
                commitAndNotify("Remove track '" + trackName + "'", ProjectState::User);
                complete("{\"success\":true}");
            });
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
