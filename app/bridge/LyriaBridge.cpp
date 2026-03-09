#include "SongbirdEditor.h"

//==============================================================================
// Bridge: Lyria AI track control
//==============================================================================

void SongbirdEditor::registerLyriaBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // ====================================================================
        // Per-track Lyria control
        // ====================================================================

        .withNativeFunction("setLyriaTrackConfig", [this](auto& args, auto complete) {
            if (args.size() < 2) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            auto config = juce::JSON::parse(args[1].toString());
            juce::MessageManager::callAsync([this, trackId, config]() { setLyriaTrackConfig(trackId, config); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setLyriaTrackPrompts", [this](auto& args, auto complete) {
            if (args.size() < 2) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            auto prompts = juce::JSON::parse(args[1].toString());
            juce::MessageManager::callAsync([this, trackId, prompts]() { setLyriaTrackPrompts(trackId, prompts); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("setLyriaQuantize", [this](auto& args, auto complete) {
            if (args.size() < 2) { complete("{\"success\":false}"); return; }
            int trackId = static_cast<int>(args[0]);
            int bars    = static_cast<int>(args[1]);
            juce::MessageManager::callAsync([this, trackId, bars]() { setLyriaQuantize(trackId, bars); });
            complete("{\"success\":true}");
        });
}
