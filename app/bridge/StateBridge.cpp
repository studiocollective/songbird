#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// Bridge: State persistence (Zustand ↔ C++)
//==============================================================================

void SongbirdEditor::registerStateBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // Load state from C++ cache (Zustand persist getItem)
        .withNativeFunction("loadState", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String storeName = args[0].toString();
                if (stateCache.count(storeName))
                    complete(stateCache[storeName]);
                else
                    complete("{\"state\":null}");
            }
        })
        // Update state on C++ side (Zustand persist setItem)
        .withNativeFunction("updateState", [this](auto& args, auto complete) {
            if (args.size() > 1) {
                juce::String storeName = args[0].toString();
                juce::String value = args[1].toString();
                // Dispatch to message thread so JUCE Timer and audio APIs work correctly
                juce::MessageManager::callAsync([this, storeName, value]() {
                    handleStateUpdate(storeName, value);
                });
                complete("ok");
            }
        })
        // React signals all Zustand stores have finished hydrating
        .withNativeFunction("reactReady", [this](auto& /*args*/, auto complete) {
            juce::MessageManager::callAsync([this]() {
                reactHydrated = true;
                DBG("StateSync: React hydrated");
            });
            complete("ok");
        })
        // Get git history for UI
        .withNativeFunction("getHistory", [this](auto& /*args*/, auto complete) {
            juce::String headHash;
            auto history = projectState.getHistory(50, &headHash);

            // Redo-tip hash
            juce::String redoTipHash;
            git_reference* redoRef = nullptr;
            if (projectState.getRepo() && git_reference_lookup(&redoRef, projectState.getRepo(), "refs/redo-tip") == 0)
            {
                char buf[GIT_OID_SHA1_HEXSIZE + 1];
                git_oid_tostr(buf, sizeof(buf), git_reference_target(redoRef));
                redoTipHash = juce::String(buf).substring(0, 7);
                git_reference_free(redoRef);
            }

            // Build commits array and find HEAD index
            juce::Array<juce::var> commits;
            int headIndex = 0;
            for (int i = 0; i < history.size(); i++)
            {
                const auto& entry = history[i];
                auto* obj = new juce::DynamicObject();
                auto shortHash = entry.hash.substring(0, 7);
                obj->setProperty("hash", shortHash);
                obj->setProperty("message", entry.message.trimEnd());
                obj->setProperty("timestamp", entry.timestamp);
                // Source tag for the UI to show who made the commit
                juce::String sourceStr = "system";
                if (entry.source == ProjectState::LLM)      sourceStr = "ai";
                else if (entry.source == ProjectState::User) sourceStr = "user";
                else if (entry.source == ProjectState::Mixer) sourceStr = "user";
                else if (entry.source == ProjectState::Plugin) sourceStr = "user";
                obj->setProperty("author", sourceStr);
                commits.add(juce::var(obj));

                if (headHash.startsWith(shortHash) || shortHash.startsWith(headHash.substring(0, 7)))
                    headIndex = i;
            }

            auto* result = new juce::DynamicObject();
            result->setProperty("commits", commits);
            result->setProperty("headIndex", headIndex);
            result->setProperty("redoTipHash", redoTipHash);
            complete(juce::JSON::toString(juce::var(result)));
        })
        // Reset state (Zustand persist removeItem)
        .withNativeFunction("resetState", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                juce::String storeName = args[0].toString();
                stateCache.erase(storeName);
                complete("ok");
            }
        });
}
