#include "BirdPopulator.h"
#include "PluginRegistry.h"
#include "MacroMapper.h"
#include "libraries/magenta/LyriaPlugin.h"

#include <algorithm>
#include <cmath>

void populateEdit(te::Edit& edit, const BirdParseResult& result, te::Engine& engine,
                  std::function<void(const juce::String&, float)> progressCallback)
{
    // Clear existing tracks settings by name
    struct TrackState {
        float volume = 1.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
    };
    std::map<juce::String, TrackState> previousStates;
    
    auto currentTracks = te::getAudioTracks(edit);
    for (auto* track : currentTracks) {
        if (track) {
            TrackState ts;
            ts.mute = track->isMuted(false);
            ts.solo = track->isSolo(false);
            if (auto volPlugin = track->getVolumePlugin()) {
                ts.volume = volPlugin->getVolumeDb();
                ts.pan = volPlugin->getPan();
            }
            previousStates[track->getName()] = ts;
        }
    }

    // Delete excess tracks
    int expectedTracks = static_cast<int>(result.channels.size()) + 4;
    for (int i = currentTracks.size() - 1; i >= expectedTracks; --i) {
        edit.deleteTrack(currentTracks[i]);
    }

    double bpm = (result.bpm > 0) ? static_cast<double>(result.bpm) : 120.0;
    edit.tempoSequence.getTempos()[0]->setBpm(bpm);

    auto fourBarsTime = te::TimePosition::fromSeconds((60.0 / bpm) * 4.0 * result.bars);

    auto& transport = edit.getTransport();
    transport.setLoopRange(te::TimeRange(te::TimePosition(), fourBarsTime));
    transport.looping = true;

    // Pre-look-up Console 1 description once
    auto console1Desc = findPluginByName(engine, CONSOLE_1.pluginName);
    if (console1Desc)
        DBG("BirdLoader: Found Console 1 - " + console1Desc->fileOrIdentifier);

    // Create a track per channel
    for (size_t i = 0; i < result.channels.size(); i++) {
        auto& ch = result.channels[i];

        edit.ensureNumberOfAudioTracks(static_cast<int>(i + 1));
        auto* track = te::getAudioTracks(edit)[static_cast<int>(i)];
        if (!track) continue;

        track->setName(juce::String(ch.name));
        
        float trackProgress = static_cast<float>(i) / static_cast<float>(result.channels.size() + 2);
        if (progressCallback) progressCallback("Loading track: " + juce::String(ch.name), trackProgress);

        // Restore track settings if we had them
        if (previousStates.find(track->getName()) != previousStates.end()) {
            auto& ts = previousStates[track->getName()];
            track->setMute(ts.mute);
            track->setSolo(ts.solo);
            if (auto volPlugin = track->getVolumePlugin()) {
                volPlugin->setVolumeDb(ts.volume); // It's already in dB
                volPlugin->setPan(ts.pan);
            }
        }

        // Determine required plugins
        juce::StringArray requiredPlugins;
        bool instrumentLoaded = false;
        
        auto pluginInfo = pluginFromKeyword(ch.plugin);
        if (pluginInfo.pluginId.isNotEmpty()) requiredPlugins.add(pluginInfo.pluginName);
        else requiredPlugins.add("4OSC");

        if (!ch.fx.empty()) {
            auto fxInfo = pluginFromKeyword(ch.fx);
            if (fxInfo.pluginId.isNotEmpty()) requiredPlugins.add(fxInfo.pluginName);
        }
        if (!ch.strip.empty()) {
            auto stripInfo = pluginFromKeyword(ch.strip);
            if (stripInfo.pluginId.isNotEmpty()) requiredPlugins.add(stripInfo.pluginName);
        }

        // Get current external plugins
        juce::StringArray currentPlugins;
        auto plugins = track->pluginList.getPlugins();
         for (auto* p : plugins) {
            if (dynamic_cast<te::ExternalPlugin*>(p)) {
                currentPlugins.add(p->getName());
            } else if (dynamic_cast<te::FourOscPlugin*>(p)) {
                currentPlugins.add("4OSC"); // Match the required side
            }
        }

        bool pluginsMatch = (requiredPlugins == currentPlugins);

        if (!pluginsMatch) {
            DBG("BirdLoader: Track '" + juce::String(ch.name) + "' plugin change [" + currentPlugins.joinIntoString(",") + "] -> [" + requiredPlugins.joinIntoString(",") + "], rebuilding");
            // Remove old plugins
            for (auto* p : plugins) {
                if (dynamic_cast<te::ExternalPlugin*>(p) || dynamic_cast<te::FourOscPlugin*>(p)) {
                    p->removeFromParent();
                }
            }

            // Add instrument plugin based on bird file `plugin` keyword
            if (pluginInfo.pluginId.isNotEmpty()) {
                if (progressCallback) progressCallback("Loading instrument: " + pluginInfo.pluginName, trackProgress + 0.05f);
                if (auto foundDesc = findPluginByName(engine, pluginInfo.pluginName)) {
                    auto extPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc);
                    if (extPlugin) {
                        track->pluginList.insertPlugin(*extPlugin, 0, nullptr);
                        instrumentLoaded = true;
                        DBG("BirdLoader: Loaded '" + foundDesc->name + "' for track '" + juce::String(ch.name) + "'");
                    }
                } else {
                    DBG("BirdLoader: Plugin '" + pluginInfo.pluginName + "' not found in scanned list — falling back to FourOsc");
                }
            }

            if (!instrumentLoaded) {
                if (auto synth = edit.getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {})) {
                    track->pluginList.insertPlugin(*synth, 0, nullptr);
                }
            }

            // Add FX plugin if specified
            if (!ch.fx.empty()) {
                auto fxInfo = pluginFromKeyword(ch.fx);
                if (fxInfo.pluginId.isNotEmpty()) {
                    if (progressCallback) progressCallback("Loading FX: " + fxInfo.pluginName, trackProgress + 0.1f);
                    if (auto foundDesc = findPluginByName(engine, fxInfo.pluginName)) {
                        if (auto fxPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                            track->pluginList.insertPlugin(*fxPlugin, -1, nullptr);
                            DBG("BirdLoader: Added FX '" + foundDesc->name + "' to track '" + juce::String(ch.name) + "'");
                        }
                    }
                }
            }

            // Add Channel Strip if specified
            if (!ch.strip.empty()) {
                auto stripInfo = pluginFromKeyword(ch.strip);
                if (stripInfo.pluginId.isNotEmpty()) {
                    if (progressCallback) progressCallback("Loading strip: " + stripInfo.pluginName, trackProgress + 0.15f);
                    if (auto foundDesc = findPluginByName(engine, stripInfo.pluginName)) {
                        if (auto stripPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                            track->pluginList.insertPlugin(*stripPlugin, -1, nullptr);
                            DBG("BirdLoader: Added Strip '" + foundDesc->name + "' to track '" + juce::String(ch.name) + "'");
                        }
                    }
                }
            }
        } else {
            DBG("BirdLoader: Plugins match, skipping tear-down for track '" + juce::String(ch.name) + "'");
        }

        // --- Handle MIDI Clip ---
        te::TimeRange clipRange(te::TimePosition(), fourBarsTime);
        auto clips = track->getClips();
        te::MidiClip* midiClip = nullptr;

        for (auto* clip : clips) {
            if (auto* mc = dynamic_cast<te::MidiClip*>(clip)) {
                midiClip = mc;
                break;
            }
        }

        if (!midiClip) {
            auto* clipBase = track->insertNewClip(
                te::TrackItem::Type::midi,
                juce::String(ch.name),
                clipRange, nullptr);
            midiClip = dynamic_cast<te::MidiClip*>(clipBase);
        } else {
            // Resize if needed (e.g. bars count changed)
            midiClip->setStart(te::TimePosition(), false, false);
            midiClip->setLength(clipRange.getLength(), false);
            midiClip->setOffset(te::TimeDuration());
            midiClip->setName(juce::String(ch.name));
        }

        if (!midiClip) continue;

        // --- Diff MIDI notes: only clear+refill if notes changed ---
        // This is the hot-path: a notes-only LLM edit costs almost nothing.
        auto& seq = midiClip->getSequence();
        const auto& existingNotes = seq.getNotes();
        bool notesChanged = (existingNotes.size() != ch.notes.size());
        if (!notesChanged) {
            for (size_t n = 0; n < ch.notes.size() && !notesChanged; ++n) {
                const auto& existing = *existingNotes[static_cast<int>(n)];
                const auto& incoming = ch.notes[n];
                if (existing.getNoteNumber() != incoming.pitch ||
                    std::abs(existing.getStartBeat().inBeats() - incoming.beatPos) > 1e-6 ||
                    std::abs(existing.getLengthBeats().inBeats() - incoming.duration) > 1e-6 ||
                    existing.getVelocity() != incoming.velocity)
                {
                    notesChanged = true;
                }
            }
        }

        if (notesChanged) {
            seq.clear(nullptr);
            double sectionBeats = result.bars * 4.0;
            bool useLoop = (ch.patternBeats > 0 && ch.patternBeats < sectionBeats);
            
            for (auto& note : ch.notes) {
                // When looping, only add notes within the first pattern pass
                if (useLoop && note.beatPos >= ch.patternBeats)
                    continue;
                seq.addNote(
                    note.pitch,
                    te::BeatPosition::fromBeats(note.beatPos),
                    te::BeatDuration::fromBeats(note.duration),
                    note.velocity,
                    0, nullptr);
            }
            
            // Set clip looping
            if (useLoop) {
                midiClip->setLoopRangeBeats(te::BeatRange(
                    te::BeatPosition(),
                    te::BeatDuration::fromBeats(ch.patternBeats)));
                midiClip->loopedSequenceType = te::MidiClip::LoopedSequenceType::loopRangeDefinesAllRepetitions;
                DBG("BirdLoader: Track '" + juce::String(ch.name) + "' looping at " + juce::String(ch.patternBeats) + " beats");
            } else {
                midiClip->disableLooping();
            }
            
            DBG("BirdLoader: Track '" + juce::String(ch.name) + "' MIDI updated (" + juce::String((int)seq.getNotes().size()) + " notes)");
        } else {
            DBG("BirdLoader: Track '" + juce::String(ch.name) + "' MIDI unchanged, skipping refill");
        }

        // --- Add Sends (Regular Tracks) ---
        for (int bus = 0; bus < 4; bus++) {
            bool found = false;
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* send = dynamic_cast<te::AuxSendPlugin*>(p)) {
                    if (send->busNumber == bus) { found = true; break; }
                }
            }
            if (!found) {
                if (auto plugin = edit.getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {})) {
                    track->pluginList.insertPlugin(*plugin, -1, nullptr);
                    auto* sendPlugin = dynamic_cast<te::AuxSendPlugin*>(plugin.get());
                    sendPlugin->busNumber = bus;
                    sendPlugin->setGainDb(-100.0f); // Default to muted
                }
            }
        }

        // --- Apply Automation Curves ---
        // Helper to find parameter
        auto applyAutomation = [&](const juce::String& macroName, const juce::String& pluginName, const std::vector<BirdAutomationPoint>& points, double timeOffsetBeats = 0.0) {
            juce::String paramId = MacroMapper::getParameterID(pluginName, macroName);
            if (paramId.isEmpty()) return;

            // Find the plugin
            te::Plugin::Ptr targetPlugin;
            for (auto* p : track->pluginList.getPlugins()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p)) {
                    if (ext->getName() == pluginName || ext->desc.name.containsIgnoreCase(pluginName)) {
                        targetPlugin = p;
                        break;
                    }
                }
            }
            if (!targetPlugin) return;

            // Find the parameter
            te::AutomatableParameter::Ptr targetParam;
            for (auto* param : targetPlugin->getAutomatableParameters()) {
                if (param->paramID == paramId || param->getPluginAndParamName() == paramId || param->getParameterName().containsIgnoreCase(paramId)) {
                    targetParam = param;
                    break;
                }
            }
            
            if (targetParam) {
                auto& curve = targetParam->getCurve();
                curve.clear(nullptr);
                
                // Write points
                for (auto& pt : points) {
                    double actualBeats = pt.time + timeOffsetBeats;
                    auto timePos = edit.tempoSequence.toTime(te::BeatPosition::fromBeats(actualBeats));
                    float val = pt.value;
                    
                    // Simple linear curve for now 
                    float curveShape = 0.0f; 
                    if (pt.shape == BirdAutomationPoint::Exponential || pt.shape == BirdAutomationPoint::Logarithmic) curveShape = 0.5f; 
                    
                    curve.addPoint(timePos, val, curveShape, nullptr);
                }
            }
        };

        // 1. Continuous section-level automation
        for (const auto& [macro, curve] : ch.automation) {
            // For now, assume it targets the main instrument plugin
            if (pluginInfo.pluginName.isNotEmpty()) {
                applyAutomation(macro, pluginInfo.pluginName, curve.points, 0.0);
            }
        }

        // 2. Step-based pattern-level automation attached to notes
        std::map<juce::String, std::vector<BirdAutomationPoint>> stepCurvesByMacro;
        
        for (const auto& note : ch.notes) {
            for (const auto& [macro, value] : note.stepParams) {
                BirdAutomationPoint pt;
                pt.time = note.beatPos;
                pt.value = value;
                pt.shape = BirdAutomationPoint::Step; // TODO parse shapes from strings
                stepCurvesByMacro[macro].push_back(pt);
            }
        }
        
        for (const auto& [macro, points] : stepCurvesByMacro) {
            if (pluginInfo.pluginName.isNotEmpty()) {
                applyAutomation(macro, pluginInfo.pluginName, points, 0.0);
            }
        }

        DBG("BirdLoader: Track " + juce::String(ch.name) +
            " — " + juce::String(static_cast<int>(ch.notes.size())) + " notes" +
            (ch.plugin.empty() ? "" : " [plugin: " + ch.plugin + "]"));
    }

    // --- Create/Configure 4 Return Tracks ---
    const juce::String returnNames[] = { "Hall", "Plate", "Delay", "Color" };
    int numRegularTracks = static_cast<int>(result.channels.size());
    for (int r = 0; r < 4; r++) {
        int trackIdx = numRegularTracks + r;
        edit.ensureNumberOfAudioTracks(trackIdx + 1);
        auto* track = te::getAudioTracks(edit)[trackIdx];
        if (!track) continue;

        track->setName(returnNames[r]);
        
        float trackProgress = static_cast<float>(numRegularTracks) / static_cast<float>(numRegularTracks + 2) + 
                              (static_cast<float>(r) / 4.0f) * (1.0f / static_cast<float>(numRegularTracks + 2));
        if (progressCallback) progressCallback("Loading " + returnNames[r], trackProgress);

        // Restore track settings if we had them
        if (previousStates.find(track->getName()) != previousStates.end()) {
            auto& ts = previousStates[track->getName()];
            track->setMute(ts.mute);
            track->setSolo(ts.solo);
            if (auto volPlugin = track->getVolumePlugin()) {
                volPlugin->setVolumeDb(ts.volume);
                volPlugin->setPan(ts.pan);
            }
        }

        // Ensure AuxReturnPlugin
        bool foundReturn = false;
        for (auto* p : track->pluginList.getPlugins()) {
            if (auto* rp = dynamic_cast<te::AuxReturnPlugin*>(p)) {
                if (rp->busNumber == r) { foundReturn = true; break; }
            }
        }
        if (!foundReturn) {
            if (auto plugin = edit.getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {})) {
                track->pluginList.insertPlugin(*plugin, 0, nullptr);
                auto* returnPlugin = dynamic_cast<te::AuxReturnPlugin*>(plugin.get());
                returnPlugin->busNumber = r;
            }
        }

        // Add specific fixed FX if missing (User Request)
        // Return 1: Long Hall (ValhallaRoom)
        // Return 2: Short Plate (ValhallaRoom)
        // Return 3: Tube Delay (Tube Delay)
        // Return 4: Overdrive (Dist TUBE-CULTURE)
        bool hasExternalFX = false;
        for (auto* p : track->pluginList.getPlugins()) {
            if (dynamic_cast<te::ExternalPlugin*>(p) != nullptr) {
                hasExternalFX = true;
                break;
            }
        }
        
        if (!hasExternalFX) {
            PluginInfo fxToAdd;
            if (r == 0) fxToAdd = REVERB_VALHALLA;
            else if (r == 1) fxToAdd = REVERB_VALHALLA; // Different preset loaded later maybe
            else if (r == 2) fxToAdd = DELAY_TUBE;
            else if (r == 3) fxToAdd = DIST_CULTURE;
            
            if (fxToAdd.pluginId.isNotEmpty()) {
                if (auto foundDesc = findPluginByName(engine, fxToAdd.pluginName)) {
                    if (auto fxPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                        track->pluginList.insertPlugin(*fxPlugin, -1, nullptr); 
                        if (progressCallback) progressCallback("Loading Return FX: " + foundDesc->name, trackProgress + 0.05f);
                        DBG("BirdLoader: Added Return FX '" + foundDesc->name + "' to 'Return " + juce::String(r + 1) + "'");
                    }
                }
            }
        }
    }

    // --- Create/Configure Master Track ---
    if (auto* master = edit.getMasterTrack()) {
        float masterProgress = static_cast<float>(numRegularTracks + 1) / static_cast<float>(numRegularTracks + 2);
        if (progressCallback) progressCallback("Loading Master Chain", masterProgress);
        const std::vector<PluginInfo> masterFX = { WEISS_DS1, CONSOLE_1 };
        
        for (const auto& fx : masterFX) {
            bool found = false;
            for (auto* p : master->pluginList.getPlugins()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(p)) {
                    if (ext->getName().containsIgnoreCase(fx.pluginName)) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found && fx.pluginId.isNotEmpty()) {
                if (auto foundDesc = findPluginByName(engine, fx.pluginName)) {
                    if (auto fxPlugin = edit.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, *foundDesc)) {
                        master->pluginList.insertPlugin(*fxPlugin, -1, nullptr);
                        if (progressCallback) progressCallback("Loading Master FX: " + foundDesc->name, masterProgress + 0.1f);
                        DBG("BirdLoader: Added Master FX '" + foundDesc->name + "'");
                    }
                }
            }
        }
    }

    DBG("BirdLoader: Loaded " + juce::String(static_cast<int>(result.channels.size())) +
        " tracks, " + juce::String(result.bars) + " bar loop");
}
