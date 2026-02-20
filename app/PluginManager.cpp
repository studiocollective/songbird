#include "SongbirdEditor.h"

//==============================================================================
// Log helper — sends to both DBG and JS console
//==============================================================================


void SongbirdEditor::logToJS(const juce::String& message)
{
    DBG(message);
    if (webView)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("message", message);
        // Serialize to JSON string so JS receives a valid JSON string to parse
        auto jsonStr = juce::JSON::toString(juce::var(obj));
        webView->emitEventIfBrowserIsVisible("cppLog", juce::var(jsonStr));
    }
}

//==============================================================================
// Plugin window management
//==============================================================================

void SongbirdEditor::openPluginWindow(int trackId, const juce::String& slotType, const juce::String& pluginId)
{
    if (!edit)
    {
        logToJS("[C++] openPluginWindow: no edit loaded");
        return;
    }

    auto audioTracks = te::getAudioTracks(*edit);
    te::Track* track = nullptr;

    if (trackId == audioTracks.size()) {
        track = edit->getMasterTrack();
    } else if (trackId >= 0 && trackId < audioTracks.size()) {
        track = audioTracks[trackId];
    }

    if (!track)
    {
        logToJS("[C++] openPluginWindow: trackId " + juce::String(trackId) + " out of range (0.." + juce::String(audioTracks.size()) + ")");
        return;
    }

    logToJS("[C++] openPluginWindow: track=" + juce::String(trackId) + " slot=" + slotType);
    logToJS("[C++]   Track '" + track->getName() + "' has " + juce::String(track->pluginList.size()) + " plugins:");
    for (auto* p : track->pluginList)
        logToJS("[C++]     - " + p->getName() + " (" + p->getPluginType() + ")"
                + (p->isMissing() ? " [MISSING]" : "")
                + (p->isDisabled() ? " [DISABLED]" : ""));

    te::Plugin::Ptr targetPlugin;

    if (slotType == "instrument")
    {
        auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
        for (auto* plugin : track->pluginList)
        {
            if (audioTrack && plugin == audioTrack->getVolumePlugin()) continue;
            if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;

            targetPlugin = plugin;
            break;
        }
    }
    else if (slotType == "fx")
    {
        for (auto* plugin : track->pluginList)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (ext->getName() == pluginId || ext->desc.fileOrIdentifier == pluginId)
                {
                    targetPlugin = plugin;
                    break;
                }
            }
        }
        
        // Fallback if not found by exact name/id (e.g. if the UI passes a generic name but the plugin desc is different)
        if (!targetPlugin)
        {
            // Just find an ExternalPlugin that is neither the first (instrument) nor the last (channel strip)
            juce::Array<te::ExternalPlugin*> extPlugins;
            for (auto* plugin : track->pluginList)
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
                    extPlugins.add(ext);
                    
            if (extPlugins.size() == 2) // Instrument and FX
                targetPlugin = extPlugins.getLast();
            else if (extPlugins.size() >= 3) // Instrument, FX, Strip
                targetPlugin = extPlugins[1];
        }
    }
    else if (slotType == "channelStrip")
    {
        te::ExternalPlugin* lastExt = nullptr;
        for (auto* plugin : track->pluginList)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
                lastExt = ext;
        }
        targetPlugin = lastExt;
    }

    if (targetPlugin)
    {
        auto name = targetPlugin->getName();
        logToJS("[C++]   Target: " + name);

        targetPlugin->showWindowExplicitly();
    }
    else
    {
        logToJS("[C++]   ✗ No plugin found for slot '" + slotType + "'");
    }
}

//==============================================================================
// Change plugin on a track
//==============================================================================

void SongbirdEditor::changePlugin(int trackId, const juce::String& slotType, const juce::String& pluginName)
{
    if (!edit) return;

    auto audioTracks = te::getAudioTracks(*edit);
    te::Track* track = nullptr;

    if (trackId == audioTracks.size()) {
        track = edit->getMasterTrack();
    } else if (trackId >= 0 && trackId < audioTracks.size()) {
        track = audioTracks[trackId];
    }

    if (!track) return;

    logToJS("[C++] changePlugin: track=" + juce::String(trackId) + " slot=" + slotType + " name='" + pluginName + "'");

    // Find the plugin description from scanned list
    auto& list = engine.getPluginManager().knownPluginList;
    std::unique_ptr<juce::PluginDescription> newDesc;

    if (pluginName.isNotEmpty())
    {
        auto lowerName = pluginName.toLowerCase();

        for (const auto& desc : list.getTypes())
            if (desc.name.toLowerCase() == lowerName)
            { newDesc = std::make_unique<juce::PluginDescription>(desc); break; }

        if (!newDesc)
            for (const auto& desc : list.getTypes())
                if (desc.name.toLowerCase().contains(lowerName))
                { newDesc = std::make_unique<juce::PluginDescription>(desc); break; }

        // On-demand scan: try to find and scan the plugin file
        if (!newDesc)
        {
            logToJS("[C++]   Plugin not cached, scanning on-demand: " + pluginName);
            
            // Try VST3 first, then AU component
            struct { juce::String path; juce::String formatName; } candidates[] = {
                { "/Library/Audio/Plug-Ins/VST3/" + pluginName + ".vst3", "VST3" },
                { "/Library/Audio/Plug-Ins/Components/" + pluginName + ".component", "AudioUnit" },
            };
            
            auto& pm = engine.getPluginManager();
            
            for (auto& candidate : candidates)
            {
                juce::File pluginFile(candidate.path);
                if (!pluginFile.exists()) continue;
                
                logToJS("[C++]   Found: " + candidate.path);
                
                for (int f = 0; f < pm.pluginFormatManager.getNumFormats(); f++)
                {
                    auto* format = pm.pluginFormatManager.getFormat(f);
                    if (!format) continue;
                    if (!format->fileMightContainThisPluginType(candidate.path)) continue;
                    
                    juce::OwnedArray<juce::PluginDescription> results;
                    format->findAllTypesForFile(results, candidate.path);
                    
                    for (auto* desc : results)
                    {
                        list.addType(*desc);
                        logToJS("[C++]   On-demand scan found: '" + desc->name + "' (" + desc->pluginFormatName + ")");
                        if (!newDesc)
                            newDesc = std::make_unique<juce::PluginDescription>(*desc);
                    }
                    if (newDesc) break;
                }
                if (newDesc) break;
            }
            
            if (!newDesc)
                logToJS("[C++]   Plugin file not found for: " + pluginName);
        }
    }

    if (slotType == "instrument")
    {
        auto* audioTrack = dynamic_cast<te::AudioTrack*>(track);
        for (auto* plugin : track->pluginList)
        {
            if (audioTrack && plugin == audioTrack->getVolumePlugin()) continue;
            if (dynamic_cast<te::LevelMeterPlugin*>(plugin)) continue;

            logToJS("[C++]   Removing old instrument: " + plugin->getName());
            plugin->deleteFromParent();
            break;
        }

        if (newDesc)
        {
            auto newPlugin = edit->getPluginCache().createNewPlugin(
                te::ExternalPlugin::xmlTypeName, *newDesc);
            if (newPlugin)
            {
                track->pluginList.insertPlugin(*newPlugin, 0, nullptr);
                logToJS("[C++]   ✓ Loaded instrument: " + newDesc->name);
            }
        }
        else if (pluginName.isEmpty())
        {
            if (auto synth = edit->getPluginCache().createNewPlugin(
                    te::FourOscPlugin::xmlTypeName, {}))
                track->pluginList.insertPlugin(*synth, 0, nullptr);
            logToJS("[C++]   Fallback to FourOsc on track " + juce::String(trackId));
        }
        else
        {
            logToJS("[C++]   ✗ Plugin '" + pluginName + "' not found in scanned list. Available plugins:");
            int count = 0;
            for (auto& type : list.getTypes())
            {
                logToJS("[C++]     - '" + type.name + "' (" + type.pluginFormatName + ")");
                if (++count >= 20) {
                    logToJS("[C++]     ... and " + juce::String(list.getNumTypes() - 20) + " more.");
                    break;
                }
            }
        }
    }
    else if (slotType == "fx")
    {
        // Try to replace the existing FX plugin
        te::ExternalPlugin* existingFx = nullptr;
        juce::Array<te::ExternalPlugin*> extPlugins;
        for (auto* plugin : track->pluginList)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
                extPlugins.add(ext);
                
        if (extPlugins.size() == 2) // Instrument and FX
            existingFx = extPlugins.getLast();
        else if (extPlugins.size() >= 3) // Instrument, FX, Strip
            existingFx = extPlugins[1];

        if (existingFx)
        {
            logToJS("[C++]   Removing old FX: " + existingFx->getName());
            existingFx->deleteFromParent();
        }

        if (newDesc)
        {
            auto newPlugin = edit->getPluginCache().createNewPlugin(
                te::ExternalPlugin::xmlTypeName, *newDesc);
            if (newPlugin)
            {
                // Insert after instrument. If there's an instrument, it's at index 0 or 1.
                // Just inserting before the channel strip if it exists.
                int insertPos = track->pluginList.size() - 1; // Default to end (before last volume/meter)
                if (extPlugins.size() > 0 && extPlugins.getLast() != existingFx) {
                    insertPos = track->pluginList.indexOf(extPlugins.getLast());
                }
                
                track->pluginList.insertPlugin(*newPlugin, insertPos, nullptr);
                logToJS("[C++]   ✓ Loaded FX: " + newDesc->name);
            }
        }
    }
    else if (slotType == "channelStrip")
    {
        te::ExternalPlugin* lastExt = nullptr;
        for (auto* plugin : track->pluginList)
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
                lastExt = ext;

        if (lastExt)
        {
            logToJS("[C++]   Removing old channel strip: " + lastExt->getName());
            lastExt->deleteFromParent();
        }

        if (newDesc)
        {
            auto newPlugin = edit->getPluginCache().createNewPlugin(
                te::ExternalPlugin::xmlTypeName, *newDesc);
            if (newPlugin)
            {
                track->pluginList.insertPlugin(*newPlugin, -1, nullptr);
                logToJS("[C++]   ✓ Loaded channel strip: " + newDesc->name);
            }
        }
    }
}
