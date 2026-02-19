#include "SongbirdEditor.h"

//==============================================================================
// Lyria generated track management
//==============================================================================

void SongbirdEditor::handleLyriaState(const juce::var& state)
{
    // Apply Lyria config/prompts to all generated tracks
    for (auto& [trackId, plugin] : lyriaPlugins)
    {
        if (!plugin) continue;

        // API key
        if (state.hasProperty("apiKey"))
        {
            juce::String key = state.getProperty("apiKey", "").toString();
            if (key.isNotEmpty())
                plugin->setApiKey(key);
        }

        // Playing state
        if (state.hasProperty("playing"))
        {
            bool shouldPlay = state.getProperty("playing", false);
            if (shouldPlay)
                plugin->play();
            else
                plugin->pause();
        }

        // Prompts
        if (state.hasProperty("prompts"))
        {
            auto promptsVar = state.getProperty("prompts", {});
            if (promptsVar.isArray())
            {
                auto* promptsArray = promptsVar.getArray();
                std::vector<magenta::Prompt> prompts;
                for (int i = 0; i < promptsArray->size(); i++)
                {
                    auto p = (*promptsArray)[i];
                    magenta::Prompt prompt;
                    prompt.id = std::to_string(i);
                    prompt.text = p.getProperty("text", "").toString().toStdString();
                    prompt.weight = static_cast<float>((double)p.getProperty("weight", 1.0));
                    prompts.push_back(prompt);
                }
                plugin->setPrompts(prompts);
            }
        }

        // Config
        if (state.hasProperty("config"))
        {
            auto configVar = state.getProperty("config", {});
            if (configVar.isObject())
            {
                magenta::LyriaConfig config;
                config.temperature = static_cast<float>((double)configVar.getProperty("temperature", 1.0));
                config.guidance = static_cast<float>((double)configVar.getProperty("guidance", 3.0));
                config.topk = static_cast<int>(configVar.getProperty("topK", 250));
                config.bpm = static_cast<int>(configVar.getProperty("bpm", 120));
                config.useBpm = (bool)configVar.getProperty("useBpm", false);
                config.density = static_cast<float>((double)configVar.getProperty("density", 0.5));
                config.useDensity = (bool)configVar.getProperty("useDensity", false);
                config.brightness = static_cast<float>((double)configVar.getProperty("brightness", 0.5));
                config.useBrightness = (bool)configVar.getProperty("useBrightness", false);
                config.muteBass = (bool)configVar.getProperty("muteBass", false);
                config.muteDrums = (bool)configVar.getProperty("muteDrums", false);
                config.muteOther = (bool)configVar.getProperty("muteOther", false);
                plugin->setConfig(config);
            }
        }
    }
}

void SongbirdEditor::addGeneratedTrack()
{
    if (!edit) return;

    int numTracks = te::getAudioTracks(*edit).size();
    edit->ensureNumberOfAudioTracks(numTracks + 1);
    auto* track = te::getAudioTracks(*edit)[numTracks];
    if (!track) return;

    int trackId = numTracks + 1;
    track->setName("Generated " + juce::String(trackId));

    // Create and add the LyriaPlugin to the track
    auto pluginInfo = te::PluginCreationInfo(*edit, track->state.getOrCreateChildWithName(
        te::IDs::PLUGIN, nullptr), true);

    auto lyriaPlugin = new magenta::LyriaPlugin(pluginInfo);
    track->pluginList.insertPlugin(*lyriaPlugin, 0, nullptr);
    lyriaPlugins[trackId] = lyriaPlugin;

    // Wire status callbacks to push to UI
    lyriaPlugin->onStatusChange = [this](bool connected, bool buffering) {
        if (webView)
        {
            juce::String js = "window.dispatchEvent(new CustomEvent('lyria-status', {detail: {connected: "
                + juce::String(connected ? "true" : "false") + ", buffering: "
                + juce::String(buffering ? "true" : "false") + "}}));";
            webView->evaluateJavascript(js, nullptr);
        }
    };

    DBG("Added generated track " + juce::String(trackId));
}

void SongbirdEditor::removeGeneratedTrack(int trackId)
{
    if (!edit) return;

    auto it = lyriaPlugins.find(trackId);
    if (it != lyriaPlugins.end())
    {
        lyriaPlugins.erase(it);
    }

    // Find and remove the track from the edit
    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId > 0 && trackId <= audioTracks.size())
    {
        auto* track = audioTracks[trackId - 1];
        if (track)
            edit->deleteTrack(track);
    }

    DBG("Removed generated track " + juce::String(trackId));
}
