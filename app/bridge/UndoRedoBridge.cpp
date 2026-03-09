#include "SongbirdEditor.h"

//==============================================================================
// Bridge: Undo / Redo / History
//==============================================================================

void SongbirdEditor::registerUndoRedoBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // ====================================================================
        // Project Undo / Redo / History
        // ====================================================================

        .withNativeFunction("undo", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() { undoProject(); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("redo", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() { redoProject(); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("revertLLM", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() { revertLLM(); });
            complete("{\"success\":true}");
        })

        .withNativeFunction("commitProject", [this](auto& args, auto complete) {
            juce::String message = args.size() > 0 ? args[0].toString() : "Manual save";
            juce::MessageManager::callAsync([this, message]() {
                saveEditState();
                commitAndNotify(message, ProjectState::User);
            });
            complete("{\"success\":true}");
        });
}
