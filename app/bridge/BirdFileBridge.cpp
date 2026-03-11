#include "SongbirdEditor.h"
#include "WebViewHelpers.h"

//==============================================================================
// Bridge: Bird file operations, BPM/scale, file save/load
//==============================================================================

void SongbirdEditor::registerBirdFileBridge(juce::WebBrowserComponent::Options& options)
{
    // Helper: rewrite the bpm/scale metadata block in the bird file.
    // Helper: rewrite the bpm/scale metadata block in the bird file.
    // Places it after the leading comment header, separated by blank lines.
    auto updateBirdMetaSection = [this]() {
        if (!currentBirdFile.existsAsFile()) return;
        auto content = currentBirdFile.loadFileAsString();
        juce::StringArray lines;
        lines.addLines(content);

        // Remove existing sig block, bare bpm/scale lines
        for (int i = lines.size() - 1; i >= 0; i--) {
            auto trimmed = lines[i].trimStart();
            if (trimmed == "sig")
                lines.remove(i);
            else if (trimmed.startsWith("bpm ") ||
                     (trimmed.startsWith("scale ") && !trimmed.startsWith("scale_")))
                lines.remove(i);
        }

        // Find the end of the leading comment header (only # lines, not blanks)
        int headerEnd = 0;
        while (headerEnd < lines.size() && lines[headerEnd].trimStart().startsWith("#"))
            headerEnd++;

        // Strip ALL blank lines between header and first content line
        while (headerEnd < lines.size() && lines[headerEnd].trim().isEmpty())
            lines.remove(headerEnd);

        // Build sig block
        bool hasBpm = lastParseResult.bpm > 0;
        bool hasScale = !lastParseResult.scaleRoot.empty() && !lastParseResult.scaleMode.empty();

        if (hasBpm || hasScale) {
            // Blank line after sig block (before content)
            lines.insert(headerEnd, "");
            // Insert indented entries (reverse order for insert-at-same-index)
            if (hasScale)
                lines.insert(headerEnd, "  scale " + juce::String(lastParseResult.scaleRoot) + " " + juce::String(lastParseResult.scaleMode));
            if (hasBpm)
                lines.insert(headerEnd, "  bpm " + juce::String(lastParseResult.bpm));
            // sig header
            lines.insert(headerEnd, "sig");
            // Blank line before sig block (after header) — only if there's a header
            if (headerEnd > 0)
                lines.insert(headerEnd, "");
        }

        auto newContent = lines.joinIntoString("\n") + "\n";
        // Skip write if nothing actually changed (avoids spurious reload triggers)
        if (newContent != content)
            currentBirdFile.replaceWithText(newContent);
    };


    options = std::move(options)
        // Set project BPM
        .withNativeFunction("setBpm", [this, updateBirdMetaSection](auto& args, auto complete) {
            if (!edit || args.size() < 1) { complete("{\"success\":false}"); return; }
            double bpm = static_cast<double>(args[0]);
            if (bpm < 20.0 || bpm > 300.0) { complete("{\"success\":false,\"error\":\"BPM out of range 20-300\"}"); return; }
            juce::MessageManager::callAsync([this, bpm, updateBirdMetaSection, complete = std::move(complete)]() mutable {
                edit->tempoSequence.getTempos()[0]->setBpm(bpm);
                lastParseResult.bpm = static_cast<int>(bpm);
                updateBirdMetaSection();
                // Notify bird file panel of content change
                if (webView)
                    webView->emitEventIfBrowserIsVisible("birdContentChanged", juce::var("ok"));
                DBG("Transport: BPM set to " + juce::String(bpm));
                complete("{\"success\":true,\"bpm\":" + juce::String(bpm) + "}");
            });
        })
        // Set project scale (lightweight — no re-parse / populateEdit)
        .withNativeFunction("setProjectScale", [this, updateBirdMetaSection](auto& args, auto complete) {
            // Args: (root: string, mode: string) or ("", "") to clear
            juce::MessageManager::callAsync([this, args, updateBirdMetaSection, complete = std::move(complete)]() mutable {
                juce::String root = args.size() > 0 ? args[0].toString() : "";
                juce::String mode = args.size() > 1 ? args[1].toString() : "";

                lastParseResult.scaleRoot = root.toStdString();
                lastParseResult.scaleMode = mode.toStdString();
                updateBirdMetaSection();

                // Notify bird file panel — no trackState emission needed,
                // scale is already set optimistically in React by setScale()
                if (webView)
                    webView->emitEventIfBrowserIsVisible("birdContentChanged", juce::var("ok"));

                DBG("Scale: set to " + root + " " + mode);
                complete("{\"success\":true}");
            });
        })
        

        // Save a .bird file (from AI generation)
        .withNativeFunction("saveBird", [this](auto& args, auto complete) {
            if (args.size() > 1) {
                juce::String relPath = args[0].toString();
                juce::String content = args[1].toString();
                
                // Resolve path relative to app location similar to constructor
                auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
                auto searchDir = appFile.getParentDirectory();
                juce::File targetFile;
                
                // Try to find the "files" directory
                juce::File filesDir;
                for (int i = 0; i < 8; i++) {
                    auto candidate = searchDir.getChildFile("files");
                    if (candidate.isDirectory()) {
                        filesDir = candidate;
                        break;
                    }
                    searchDir = searchDir.getParentDirectory();
                }
                
                if (filesDir.isDirectory()) {
                    targetFile = filesDir.getChildFile(relPath);
                } else {
                    // Fallback to CWD
                    targetFile = juce::File::getCurrentWorkingDirectory().getChildFile(relPath);
                }

                juce::MessageManager::callAsync([this, targetFile, content]() {
                    if (!targetFile.existsAsFile())
                        targetFile.create();
                    targetFile.replaceWithText(content);
                    currentBirdFile = targetFile;

                    // Soft reload: parse + populateEdit (diffs against live state)
                    auto result = BirdLoader::parse(currentBirdFile.getFullPathName().toStdString());
                    if (result.error.empty()) {
                        BirdLoader::populateEdit(*edit, result, engine, nullptr);
                        lastParseResult = result;
                    }
                    emitTrackState();
                });
            }
            complete("ok");
        });
}
