#include "SongbirdEditor.h"

//==============================================================================
// Lyria generated track management
//==============================================================================

void SongbirdEditor::handleLyriaState(const juce::var& state)
{
    // Apply Lyria config/prompts to all generated tracks (global update)
    for (auto& [trackId, ctx] : lyriaPlugins)
    {
        auto* plugin = ctx.plugin;
        if (!plugin) continue;

        if (state.hasProperty("apiKey"))
        {
            juce::String key = state.getProperty("apiKey", "").toString();
            if (key.isNotEmpty())
                plugin->setApiKey(key);
        }

        if (state.hasProperty("playing"))
        {
            bool shouldPlay = state.getProperty("playing", false);
            if (shouldPlay) plugin->play(); else plugin->pause();
        }

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
                    prompt.id     = std::to_string(i);
                    prompt.text   = p.getProperty("text",   "").toString().toStdString();
                    prompt.weight = static_cast<float>((double)p.getProperty("weight", 1.0));
                    prompts.push_back(prompt);
                }
                plugin->setPrompts(prompts);
            }
        }

        if (state.hasProperty("config"))
        {
            auto configVar = state.getProperty("config", {});
            if (configVar.isObject())
            {
                magenta::LyriaConfig config;
                config.temperature   = static_cast<float>((double)configVar.getProperty("temperature",   1.0));
                config.guidance      = static_cast<float>((double)configVar.getProperty("guidance",      3.0));
                config.topk          = static_cast<int>(configVar.getProperty("topK",                    250));
                config.bpm           = static_cast<int>(configVar.getProperty("bpm",                     120));
                config.useBpm        = (bool)configVar.getProperty("useBpm",        false);
                config.density       = static_cast<float>((double)configVar.getProperty("density",       0.5));
                config.useDensity    = (bool)configVar.getProperty("useDensity",    false);
                config.brightness    = static_cast<float>((double)configVar.getProperty("brightness",    0.5));
                config.useBrightness = (bool)configVar.getProperty("useBrightness", false);
                config.muteBass      = (bool)configVar.getProperty("muteBass",      false);
                config.muteDrums     = (bool)configVar.getProperty("muteDrums",     false);
                config.muteOther     = (bool)configVar.getProperty("muteOther",     false);
                plugin->setConfig(config);
            }
        }
    }
}

//==============================================================================
// Per-track Lyria control (called from WebViewBridge / ChatPanel AI tools)
//==============================================================================

void SongbirdEditor::setLyriaTrackConfig(int trackId, const juce::var& config)
{
    auto it = lyriaPlugins.find(trackId);
    if (it == lyriaPlugins.end() || !it->second.plugin) return;

    auto* plugin = it->second.plugin;

    magenta::LyriaConfig lConfig;
    lConfig.temperature   = static_cast<float>((double)config.getProperty("temperature",   1.0));
    lConfig.guidance      = static_cast<float>((double)config.getProperty("guidance",      3.0));
    lConfig.topk          = static_cast<int>(config.getProperty("topK",                    250));
    lConfig.bpm           = static_cast<int>(config.getProperty("bpm",                     120));
    lConfig.useBpm        = (bool)config.getProperty("useBpm",        false);
    lConfig.density       = static_cast<float>((double)config.getProperty("density",       0.5));
    lConfig.useDensity    = (bool)config.getProperty("useDensity",    false);
    lConfig.brightness    = static_cast<float>((double)config.getProperty("brightness",    0.5));
    lConfig.useBrightness = (bool)config.getProperty("useBrightness", false);
    lConfig.muteBass      = (bool)config.getProperty("muteBass",      false);
    lConfig.muteDrums     = (bool)config.getProperty("muteDrums",     false);
    lConfig.muteOther     = (bool)config.getProperty("muteOther",     false);
    plugin->setConfig(lConfig);

    DBG("LyriaManager: setConfig track=" + juce::String(trackId));
}

void SongbirdEditor::setLyriaTrackPrompts(int trackId, const juce::var& promptsVar)
{
    auto it = lyriaPlugins.find(trackId);
    if (it == lyriaPlugins.end() || !it->second.plugin) return;

    auto* plugin = it->second.plugin;
    if (!promptsVar.isArray()) return;

    auto* arr = promptsVar.getArray();
    std::vector<magenta::Prompt> prompts;
    for (int i = 0; i < arr->size(); ++i)
    {
        auto p = (*arr)[i];
        magenta::Prompt pm;
        pm.id     = std::to_string(i);
        pm.text   = p.getProperty("text",   "").toString().toStdString();
        pm.weight = static_cast<float>((double)p.getProperty("weight", 1.0));
        prompts.push_back(pm);
    }
    plugin->setPrompts(prompts);

    DBG("LyriaManager: setPrompts track=" + juce::String(trackId) + " count=" + juce::String((int)prompts.size()));
}

void SongbirdEditor::setLyriaQuantize(int trackId, int bars)
{
    auto it = lyriaPlugins.find(trackId);
    if (it == lyriaPlugins.end()) return;

    it->second.quantizeBars = bars;
    DBG("LyriaManager: setQuantize track=" + juce::String(trackId) + " bars=" + juce::String(bars));
    // Quantization is enforced in timerCallback (below) – when the transport crosses
    // a bar boundary, Lyria is paused and re-started cleanly.
}

//==============================================================================
// Track add / remove
//==============================================================================

void SongbirdEditor::addGeneratedTrack()
{
    if (!edit) return;

    int numTracks = te::getAudioTracks(*edit).size();
    edit->ensureNumberOfAudioTracks(numTracks + 1);
    auto* track = te::getAudioTracks(*edit)[numTracks];
    if (!track) return;

    int trackId = numTracks + 1;
    track->setName("Generated " + juce::String(trackId));

    auto pluginInfo = te::PluginCreationInfo(*edit, track->state.getOrCreateChildWithName(
        te::IDs::PLUGIN, nullptr), true);

    auto lyriaPlugin = new magenta::LyriaPlugin(pluginInfo);
    track->pluginList.insertPlugin(*lyriaPlugin, 0, nullptr);

    LyriaTrackContext ctx;
    ctx.plugin       = lyriaPlugin;
    ctx.quantizeBars = 0;
    lyriaPlugins[trackId] = ctx;

    lyriaPlugin->onStatusChange = [this](bool connected, bool buffering) {
        if (webView)
        {
            juce::String js = "window.dispatchEvent(new CustomEvent('lyria-status', {detail: {connected: "
                + juce::String(connected ? "true" : "false") + ", buffering: "
                + juce::String(buffering ? "true" : "false") + "}}));";
            webView->evaluateJavascript(js, nullptr);
        }
    };

    DBG("LyriaManager: Added generated track " + juce::String(trackId));
}

void SongbirdEditor::removeGeneratedTrack(int trackId)
{
    if (!edit) return;

    auto it = lyriaPlugins.find(trackId);
    if (it != lyriaPlugins.end())
        lyriaPlugins.erase(it);

    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId > 0 && trackId <= (int)audioTracks.size())
    {
        auto* track = audioTracks[trackId - 1];
        if (track)
            edit->deleteTrack(track);
    }

    DBG("LyriaManager: Removed generated track " + juce::String(trackId));
}
