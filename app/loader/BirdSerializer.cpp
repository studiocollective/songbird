#include "BirdSerializer.h"
#include "PluginRegistry.h"
#include "libraries/magenta/LyriaPlugin.h"

juce::String getTrackStateJSON(te::Edit& edit, const BirdParseResult* parseResult) {
    auto tracks = te::getAudioTracks(edit);
    juce::String json = "{\"tracks\":[";

    int trackCount = 0;
    for (int t = 0; t <= tracks.size(); t++) {
        te::Track* track = nullptr;
        bool isMaster = (t == tracks.size());
        
        if (isMaster) {
            track = edit.getMasterTrack();
        } else {
            track = tracks[t];
        }

        if (!track) continue;

        // Skip the master track in the regular loop — it's handled separately at the end
        if (!isMaster && dynamic_cast<te::MasterTrack*>(track)) continue;

        if (trackCount > 0) json += ",";
        trackCount++;

        // For return/master tracks: read the actual ExternalPlugins from the plugin list directly
        bool isReturn = !isMaster && (track->getName() == "Hall" || track->getName() == "Plate" || track->getName() == "Delay" || track->getName() == "Color");

        // Look up plugin info from parse result (regular tracks only)
        juce::String pluginField;
        juce::String fxField;
        juce::String channelStripField;
        
        if (!isMaster && !isReturn && parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto& parsedCh = parseResult->channels[t];
            
            auto info = pluginFromKeyword(parsedCh.plugin);
            if (info.pluginId.isNotEmpty()) {
                pluginField = ",\"plugin\":{\"pluginId\":" + juce::JSON::toString(info.pluginId) +
                              ",\"pluginName\":" + juce::JSON::toString(info.pluginName) + "}";
            }
            
            auto fxInfo = pluginFromKeyword(parsedCh.fx);
            if (fxInfo.pluginId.isNotEmpty()) {
                fxField = ",\"fx\":{\"pluginId\":" + juce::JSON::toString(fxInfo.pluginId) +
                          ",\"pluginName\":" + juce::JSON::toString(fxInfo.pluginName) + "}";
            }

            auto stripInfo = pluginFromKeyword(parsedCh.strip);
            if (stripInfo.pluginId.isNotEmpty()) {
                channelStripField = ",\"channelStrip\":{\"pluginId\":" + juce::JSON::toString(stripInfo.pluginId) +
                                    ",\"pluginName\":" + juce::JSON::toString(stripInfo.pluginName) + "}";
            }
        }

        if (isReturn || isMaster) {
            juce::Array<te::ExternalPlugin*> extPlugins;
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p))
                    extPlugins.add(ext);
            }
            if (extPlugins.size() >= 1) {
                auto name = extPlugins[0]->getName();
                fxField = juce::String(",\"fx\":{\"pluginId\":\"\",\"pluginName\":") + juce::JSON::toString(name) + "}";
            }
            if (extPlugins.size() >= 2) {
                auto name = extPlugins[1]->getName();
                channelStripField = juce::String(",\"channelStrip\":{\"pluginId\":\"\",\"pluginName\":") + juce::JSON::toString(name) + "}";
            }
        }

        juce::String sendsJson = "[0.0, 0.0, 0.0, 0.0]";
        if (!isReturn && !isMaster) {
            juce::Array<float> sendVals = { 0.0f, 0.0f, 0.0f, 0.0f };
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p)) {
                    if (send->busNumber >= 0 && send->busNumber < 4) {
                        sendVals.set(send->busNumber, juce::Decibels::decibelsToGain(send->getGainDb()));
                    }
                }
            }
            sendsJson = "[" + juce::String(sendVals[0], 2) + "," + 
                              juce::String(sendVals[1], 2) + "," + 
                              juce::String(sendVals[2], 2) + "," + 
                              juce::String(sendVals[3], 2) + "]";
        }

        // Determine the track type to emit
        // Priority: parse result trackType > Lyria plugin detection > default "midi"
        juce::String trackTypeStr = "midi"; // default for BirdLoader-owned tracks
        if (!isMaster && !isReturn && parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto& parsedCh = parseResult->channels[t];
            if (!parsedCh.trackType.empty())
                trackTypeStr = juce::String(parsedCh.trackType);
        } else if (isReturn) {
            trackTypeStr = "midi"; // returns are always midi
        }
        // If a LyriaPlugin is present on this track, override to gen-audio
        if (!isMaster && !isReturn && track) {
            for (auto* p : track->pluginList.getPlugins()) {
                if (dynamic_cast<magenta::LyriaPlugin*>(p)) {
                    trackTypeStr = "audio";
                    break;
                }
            }
        }

        // Read volume/pan from Tracktion for this track
        int trackVolume = 80;
        int trackPan = 0;
        bool trackMuted = false;
        bool trackSolo = false;
        if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track)) {
            if (auto vp = audioTrack->getVolumePlugin()) {
                trackVolume = juce::roundToInt(juce::Decibels::decibelsToGain(vp->getVolumeDb()) * 127.0f);
                trackPan = juce::roundToInt(vp->getPan() * 64.0f);
            }
            trackMuted = audioTrack->isMuted(false);
            trackSolo = audioTrack->isSolo(false);
        }

        json += "{\"id\":" + juce::String(t) +
                ",\"name\":" + juce::JSON::toString(isMaster ? "Master" : track->getName()) +
                ",\"trackType\":" + juce::JSON::toString(trackTypeStr) +
                ",\"isReturn\":" + (isReturn ? "true" : "false") +
                ",\"isMaster\":" + (isMaster ? "true" : "false") +
                ",\"volume\":" + juce::String(trackVolume) +
                ",\"pan\":" + juce::String(trackPan) +
                ",\"muted\":" + (trackMuted ? "true" : "false") +
                ",\"solo\":" + (trackSolo ? "true" : "false") +
                ",\"sends\":" + sendsJson +
                pluginField + fxField + channelStripField +
                ",\"notes\":[";

        if (!isMaster) {
            if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track)) {
                auto clips = audioTrack->getClips();
                int noteCount = 0;
                for (auto* clip : clips) {
                    auto* midiClip = dynamic_cast<te::MidiClip*>(clip);
                    if (!midiClip) continue;

                    auto& seq = midiClip->getSequence();
                    for (int n = 0; n < seq.getNumNotes(); n++) {
                        auto* note = seq.getNote(n);
                        if (noteCount > 0) json += ",";
                        json += "{\"pitch\":" + juce::String(note->getNoteNumber()) +
                                ",\"beat\":" + juce::String(note->getStartBeat().inBeats(), 3) +
                                ",\"duration\":" + juce::String(note->getLengthBeats().inBeats(), 3) +
                                ",\"velocity\":" + juce::String(note->getVelocity()) + "}";
                        noteCount++;
                    }
                }
            }
        }
        
        // Read loop length from first MidiClip (if looping)
        double loopLenBeats = 0;
        if (!isMaster) {
            if (auto* audioTrack = dynamic_cast<te::AudioTrack*>(track)) {
                for (auto* clip : audioTrack->getClips()) {
                    if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                        if (mc->isLooping())
                            loopLenBeats = mc->getLoopLengthBeats().inBeats();
                        break;
                    }
                }
            }
        }
        
        json += "],\"loopLengthBeats\":" + juce::String(loopLenBeats, 3) +
                ",\"automation\":[";

        if (!isMaster && parseResult && t < static_cast<int>(parseResult->channels.size())) {
            auto& parsedCh = parseResult->channels[t];
            
            // Collect all step automation into curves by macro
            std::map<juce::String, std::vector<BirdAutomationPoint>> stepCurvesByMacro;
            for (const auto& note : parsedCh.notes) {
                for (const auto& [macro, value] : note.stepParams) {
                    BirdAutomationPoint pt;
                    pt.time = note.beatPos;
                    pt.value = value;
                    pt.shape = BirdAutomationPoint::Step;
                    stepCurvesByMacro[macro].push_back(pt);
                }
            }
            
            int macroCount = 0;
            // 1. Continuous section curves
            for (const auto& [macro, curve] : parsedCh.automation) {
                if (macroCount > 0) json += ",";
                json += "{\"macro\":" + juce::JSON::toString(juce::String(macro)) + ",\"points\":[";
                for (size_t i = 0; i < curve.points.size(); ++i) {
                    if (i > 0) json += ",";
                    auto& pt = curve.points[i];
                    json += "{\"time\":" + juce::String(pt.time, 3) + 
                            ",\"value\":" + juce::String(pt.value, 3) + 
                            ",\"shape\":" + juce::String(pt.shape) + "}";
                }
                json += "]}";
                macroCount++;
            }
            // 2. Step curves
            for (const auto& [macro, points] : stepCurvesByMacro) {
                // Skip if this macro was already written as a continuous curve
                if (parsedCh.automation.find(macro.toStdString()) != parsedCh.automation.end()) continue;
                
                if (macroCount > 0) json += ",";
                json += "{\"macro\":" + juce::JSON::toString(macro) + ",\"points\":[";
                for (size_t i = 0; i < points.size(); ++i) {
                    if (i > 0) json += ",";
                    auto& pt = points[i];
                    json += "{\"time\":" + juce::String(pt.time, 3) + 
                            ",\"value\":" + juce::String(pt.value, 3) + 
                            ",\"shape\":" + juce::String(pt.shape) + "}";
                }
                json += "]}";
                macroCount++;
            }
        }

        json += "]}";
    }

    json += "],\"sections\":[";

    // Add sections from arrangement
    if (parseResult && !parseResult->arrangement.empty()) {
        int barOffset = 0;
        for (size_t i = 0; i < parseResult->arrangement.size(); i++) {
            auto& entry = parseResult->arrangement[i];
            if (i > 0) json += ",";
            json += "{\"name\":" + juce::JSON::toString(juce::String(entry.sectionName)) +
                    ",\"start\":" + juce::String(barOffset) +
                    ",\"length\":" + juce::String(entry.bars) + "}";
            barOffset += entry.bars;
        }
    }

    json += "],\"totalBars\":" + juce::String(parseResult ? parseResult->bars : 1);

    if (parseResult && parseResult->hasKeySignature) {
        json += ",\"keySignature\":" + juce::JSON::toString(juce::String(parseResult->keyName));
    }

    if (parseResult && parseResult->bpm > 0) {
        json += ",\"bpm\":" + juce::String(parseResult->bpm);
    }

    if (parseResult && !parseResult->scaleRoot.empty() && !parseResult->scaleMode.empty()) {
        json += ",\"scale\":{\"root\":" + juce::JSON::toString(juce::String(parseResult->scaleRoot))
              + ",\"mode\":" + juce::JSON::toString(juce::String(parseResult->scaleMode)) + "}";
    }

    json += "}";
    return json;
}
