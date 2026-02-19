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

void SongbirdEditor::openPluginWindow(int trackId, const juce::String& slotType, const juce::String& /*pluginId*/)
{
    if (!edit)
    {
        logToJS("[C++] openPluginWindow: no edit loaded");
        return;
    }

    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId < 0 || trackId >= audioTracks.size())
    {
        logToJS("[C++] openPluginWindow: trackId " + juce::String(trackId) + " out of range (0.." + juce::String(audioTracks.size() - 1) + ")");
        return;
    }

    auto* track = audioTracks[trackId];
    if (!track) return;

    logToJS("[C++] openPluginWindow: track=" + juce::String(trackId) + " slot=" + slotType);
    logToJS("[C++]   Track '" + track->getName() + "' has " + juce::String(track->pluginList.size()) + " plugins:");
    for (auto* p : track->pluginList)
        logToJS("[C++]     - " + p->getName() + " (" + p->getPluginType() + ")"
                + (p->isMissing() ? " [MISSING]" : "")
                + (p->isDisabled() ? " [DISABLED]" : ""));

    te::Plugin::Ptr targetPlugin;

    if (slotType == "instrument")
    {
        for (auto* plugin : track->pluginList)
        {
            if (plugin != track->getVolumePlugin()
                && !dynamic_cast<te::LevelMeterPlugin*>(plugin))
            {
                targetPlugin = plugin;
                break;
            }
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
        bool missing = targetPlugin->isMissing();
        bool disabled = targetPlugin->isDisabled();

        logToJS("[C++]   Target: " + name
                + " missing=" + juce::String(missing ? "YES" : "no")
                + " disabled=" + juce::String(disabled ? "YES" : "no"));

        targetPlugin->showWindowExplicitly();

        // Check for window immediately
        logToJS("[C++]   Checking visual status immediately...");
        auto& desktop = juce::Desktop::getInstance();
        bool foundImmediate = false;
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* c = desktop.getComponent(i);
            if (c->getName().containsIgnoreCase(name))
            {
                c->toFront(true);
                c->setAlwaysOnTop(true);
                foundImmediate = true;
                logToJS("[C++]   ✓ Found and forced to front (immediate): " + c->getName());
                break;
            }
        }

        if (!foundImmediate)
        {
            logToJS("[C++]   Not found immediately. Scheduling delayed check (200ms)...");
            
            // Capture name by value, safe checking
            juce::Timer::callAfterDelay(200, [this, name]() {
                logToJS("[C++]   Delayed check for '" + name + "' window:");
                auto& d = juce::Desktop::getInstance();
                bool found = false;
                for (int i = 0; i < d.getNumComponents(); ++i)
                {
                    auto* c = d.getComponent(i);
                    logToJS("[C++]     - " + c->getName() + " (visible=" + juce::String((int)c->isVisible()) + ")");
                    
                    if (c->getName().containsIgnoreCase(name))
                    {
                        c->toFront(true);
                        c->setAlwaysOnTop(true);
                        found = true;
                        logToJS("[C++]   ✓ Found and forced to front (delayed): " + c->getName());
                        break;
                    }
                }
                if (!found) logToJS("[C++]   ✗ Still not found after delay.");
            });
        }
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
    if (trackId < 0 || trackId >= audioTracks.size()) return;

    auto* track = audioTracks[trackId];
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
        for (auto* plugin : track->pluginList)
        {
            if (plugin != track->getVolumePlugin()
                && !dynamic_cast<te::LevelMeterPlugin*>(plugin))
            {
                logToJS("[C++]   Removing old instrument: " + plugin->getName());
                plugin->deleteFromParent();
                break;
            }
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
