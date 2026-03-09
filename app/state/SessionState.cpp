#include "SongbirdEditor.h"
#include "ValueTreeJSON.h"

//==============================================================================
// State cache, session state, and edit state persistence
//==============================================================================

void SongbirdEditor::commitAndNotify(const juce::String& message, ProjectState::Source source, bool includeEditXml)
{
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    projectState.commit(message, source, includeEditXml);
    DBG("  commitAndNotify: git commit took " + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 0) + "ms (" + message + ")");
    if (webView)
        webView->emitEventIfBrowserIsVisible("historyChanged", juce::var("ok"));
}

void SongbirdEditor::saveStateCache()
{
    if (!currentBirdFile.existsAsFile()) return;
    
    // Only save mixer state to daw.state.json (git-tracked for undo/redo)
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    auto it = stateCache.find("songbird-mixer");
    if (it != stateCache.end())
        obj->setProperty(it->first, juce::JSON::parse(it->second));
        
    auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".state.json");
    if (obj->getProperties().size() > 0)
    {
        stateFile.replaceWithText(juce::JSON::toString(obj.get()));
    }
}

void SongbirdEditor::saveSessionState()
{
    if (!currentBirdFile.existsAsFile()) return;
    
    // Save transport/chat/lyria to daw.session.json (gitignored, not part of undo)
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    for (auto& pair : stateCache)
    {
        if (pair.first == "songbird-mixer") continue;  // mixer is in state.json
        obj->setProperty(pair.first, juce::JSON::parse(pair.second));
    }
        
    auto sessionFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".session.json");
    if (obj->getProperties().size() > 0)
    {
        sessionFile.replaceWithText(juce::JSON::toString(obj.get()));
    }
}

void SongbirdEditor::loadStateCache()
{
    if (!currentBirdFile.existsAsFile()) return;
    stateCache.clear();
    
    // Load git-tracked mixer state from daw.state.json
    auto stateFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".state.json");
    
    // Backward compatibility: migrate old daw.bird.state.json → daw.state.json
    if (!stateFile.existsAsFile()) {
        auto oldFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileName() + ".state.json");
        if (oldFile.existsAsFile())
            oldFile.moveFileTo(stateFile);
    }
    if (stateFile.existsAsFile())
    {
        auto parsed = juce::JSON::parse(stateFile);
        if (auto* obj = parsed.getDynamicObject())
        {
            for (auto& prop : obj->getProperties())
                stateCache[prop.name.toString()] = juce::JSON::toString(prop.value);
        }
    }
    
    // Load session state (transport/chat/lyria) from daw.session.json
    auto sessionFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".session.json");
    if (sessionFile.existsAsFile())
    {
        auto parsed = juce::JSON::parse(sessionFile);
        if (auto* obj = parsed.getDynamicObject())
        {
            for (auto& prop : obj->getProperties())
                stateCache[prop.name.toString()] = juce::JSON::toString(prop.value);
        }
    }
    
    DBG("StateSync: Loaded state cache (" + juce::String((int)stateCache.size()) + " stores)");
}

void SongbirdEditor::saveEditState()
{
    if (!edit || !currentBirdFile.existsAsFile()) return;
    if (dirtyPlugins.empty())
    {
        DBG("EditState: All plugins clean, skipping save");
        return;
    }

    // Only flush plugins that actually changed (not all 50+)
    int flushed = 0;
    for (auto* plugin : dirtyPlugins)
    {
        if (plugin != nullptr)
        {
            edit->flushPluginStateIfNeeded(*plugin);
            flushed++;
        }
    }
    dirtyPlugins.clear();

    auto editFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.json");
    editFile.replaceWithText(ValueTreeJSON::toJsonString(edit->state));
    DBG("EditState: Flushed " + juce::String(flushed) + " dirty plugins, saved to " + editFile.getFullPathName());
}

void SongbirdEditor::loadEditState()
{
    if (!edit || !currentBirdFile.existsAsFile()) return;
    auto t0 = juce::Time::getMillisecondCounterHiRes();

    auto editFile = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.json");
    
    // Backward compatibility: migrate old .edit.xml if .edit.json doesn't exist
    if (!editFile.existsAsFile()) {
        auto oldXml = currentBirdFile.getSiblingFile(currentBirdFile.getFileNameWithoutExtension() + ".edit.xml");
        if (oldXml.existsAsFile()) {
            auto xml = juce::parseXML(oldXml);
            if (xml) {
                auto tree = juce::ValueTree::fromXml(*xml);
                editFile.replaceWithText(ValueTreeJSON::toJsonString(tree));
                oldXml.deleteFile();
                DBG("EditState: Migrated .edit.xml -> .edit.json");
            }
        }
    }
    if (!editFile.existsAsFile()) return;

    auto jsonText = editFile.loadFileAsString();
    DBG("    loadEdit: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] file read (" + juce::String(jsonText.length()) + " bytes)");
    
    auto savedState = ValueTreeJSON::fromJsonString(jsonText);
    DBG("    loadEdit: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] JSON parse + ValueTree");
    
    if (!savedState.isValid()) {
        DBG("    loadEdit: INVALID saved state!");
        return;
    }

    // Build a map of saved tracks: trackName -> ValueTree
    std::map<juce::String, juce::ValueTree> savedTracks;
    for (int i = 0; i < savedState.getNumChildren(); i++)
    {
        auto child = savedState.getChild(i);
        if (child.hasType("TRACK") || child.hasType("MASTERTRACK"))
        {
            juce::String name = child.getProperty("name", "").toString();
            if (name.isNotEmpty())
                savedTracks[name] = child;
        }
    }
    DBG("    loadEdit: " + juce::String((int)savedTracks.size()) + " saved tracks, " 
        + juce::String(te::getAllTracks(*edit).size()) + " live tracks");

    // Restore plugin states — only for plugins whose state actually changed
    int restored = 0, skipped = 0;
    
    auto restorePluginsForTrack = [&](te::Track* track)
    {
        if (!track) return;
        auto it = savedTracks.find(track->getName());
        if (it == savedTracks.end()) return;

        auto& savedTrackVT = it->second;

        for (auto* livePlugin : track->pluginList.getPlugins())
        {
            auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(livePlugin);
            if (!extPlugin) continue;

            juce::String liveName = extPlugin->getName();

            // Find matching plugin in saved state (plugins are direct TRACK children)
            for (int p = 0; p < savedTrackVT.getNumChildren(); p++)
            {
                auto savedPluginVT = savedTrackVT.getChild(p);
                if (!savedPluginVT.hasType("PLUGIN")) continue;

                // Match by name — Tracktion uses "vst" not "external" for type,
                // so just match on plugin name (live-side dynamic_cast already filters)
                juce::String savedName = savedPluginVT.getProperty("name", "").toString();
                if (savedName != liveName) continue;

                // Compare state blobs — skip restore if unchanged
                auto savedBlob = savedPluginVT.getProperty("state");
                auto& liveVT = extPlugin->state;
                auto liveBlob = liveVT.getProperty("state");
                
                if (savedBlob == liveBlob) {
                    skipped++;
                } else {
                    extPlugin->restorePluginStateFromValueTree(savedPluginVT);
                    restored++;
                }
                break;
            }
        }
    };

    // Restore for all audio tracks
    for (auto* track : te::getAllTracks(*edit))
        restorePluginsForTrack(track);

    // Restore for master track
    restorePluginsForTrack(edit->getMasterTrack());

    DBG("    loadEdit: [" + juce::String(juce::Time::getMillisecondCounterHiRes() - t0, 1) + "ms] " 
        + juce::String(restored) + " restored, " + juce::String(skipped) + " unchanged");
}
