#include "SongbirdEditor.h"

//==============================================================================
// Reactive plugin state tracking
//==============================================================================

void SongbirdEditor::registerPluginListeners()
{
    if (!edit) return;
    unregisterPluginListeners();  // clear any stale listeners
    
    auto registerForTrack = [this](te::Track* track)
    {
        if (!track) return;
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (auto* proc = ext->getAudioPluginInstance())
                    proc->addListener(this);
            }
        }
    };
    
    for (auto* track : te::getAllTracks(*edit))
        registerForTrack(track);
    registerForTrack(edit->getMasterTrack());
    
    // Count registered plugins for logging
    int count = 0;
    for (auto* track : te::getAllTracks(*edit))
        for (auto* plugin : track->pluginList.getPlugins())
            if (dynamic_cast<te::ExternalPlugin*>(plugin))
                count++;
    
    // Plugins start clean — disk matches memory after load.
    // Listeners will mark specific plugins dirty when they actually change.
    dirtyPlugins.clear();
    dirtyPluginParams.clear();
    pluginParamsDirty = false;
    
    DBG("EditState: Registered listeners on " + juce::String(count) + " plugins (all clean)");
}

void SongbirdEditor::unregisterPluginListeners()
{
    if (!edit) return;
    
    auto unregisterForTrack = [this](te::Track* track)
    {
        if (!track) return;
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (auto* proc = ext->getAudioPluginInstance())
                    proc->removeListener(this);
            }
        }
    };
    
    for (auto* track : te::getAllTracks(*edit))
        unregisterForTrack(track);
    unregisterForTrack(edit->getMasterTrack());
}

te::ExternalPlugin* SongbirdEditor::findExternalPlugin(juce::AudioProcessor* processor)
{
    if (!edit || !processor) return nullptr;
    
    auto findInTrack = [processor](te::Track* track) -> te::ExternalPlugin*
    {
        if (!track) return nullptr;
        for (auto* plugin : track->pluginList.getPlugins())
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                if (ext->getAudioPluginInstance() == processor)
                    return ext;
            }
        }
        return nullptr;
    };
    
    for (auto* track : te::getAllTracks(*edit))
        if (auto* found = findInTrack(track))
            return found;
    return findInTrack(edit->getMasterTrack());
}

// Called from audio thread — must be lock-free
void SongbirdEditor::audioProcessorParameterChanged(juce::AudioProcessor* processor, int paramIndex, float)
{
    // Capture param name on audio thread before posting (lightweight string copy)
    juce::String paramName;
    if (auto* param = processor->getParameters()[paramIndex])
        paramName = param->getName(64);

    // Post to message thread for safe dirty marking
    juce::MessageManager::callAsync([this, processor, paramName]()
    {
        if (undoRedoInProgress.load()) return;
        if (auto* ext = findExternalPlugin(processor))
        {
            dirtyPlugins.insert(ext);
            if (paramName.isNotEmpty())
                dirtyPluginParams[ext->getName()].insert(paramName);
            pluginParamsDirty = true;
            startTimer(500);
        }
    });
}

// Called when plugin signals a major internal state change (UI open, preset load, etc.)
// Only commit when a meaningful flag is set — UI open/close fires with all flags false
void SongbirdEditor::audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details)
{
    bool meaningful = details.programChanged || details.nonParameterStateChanged
                   || details.parameterInfoChanged || details.latencyChanged;
    juce::MessageManager::callAsync([this, processor, meaningful, details]()
    {
        if (undoRedoInProgress.load()) return;
        if (auto* ext = findExternalPlugin(processor))
        {
            dirtyPlugins.insert(ext);
            if (meaningful)
            {
                auto label = details.programChanged ? "preset change"
                           : details.nonParameterStateChanged ? "state change"
                           : "config change";
                dirtyPluginParams[ext->getName()].insert(label);
                pluginParamsDirty = true;
            }
            startTimer(500);
        }
    });
}
