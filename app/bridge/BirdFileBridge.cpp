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

        // Remove existing bpm and scale lines
        for (int i = lines.size() - 1; i >= 0; i--) {
            auto trimmed = lines[i].trimStart();
            if (trimmed.startsWith("bpm ") ||
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

        // Build meta lines
        juce::StringArray meta;
        if (lastParseResult.bpm > 0)
            meta.add("bpm " + juce::String(lastParseResult.bpm));
        if (!lastParseResult.scaleRoot.empty() && !lastParseResult.scaleMode.empty())
            meta.add("scale " + juce::String(lastParseResult.scaleRoot) + " " + juce::String(lastParseResult.scaleMode));

        // Insert meta section after the header with single blank line separators
        if (meta.size() > 0) {
            // Blank line after meta block (before content)
            lines.insert(headerEnd, "");
            // Insert meta lines
            for (int i = meta.size() - 1; i >= 0; i--)
                lines.insert(headerEnd, meta[i]);
            // Blank line before meta block (after header) — only if there's a header
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
        
        // Explicit Transport Controls
        .withNativeFunction("transportPlay", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    edit->getTransport().play(false);
                    DBG("Transport: Playing (Native)");
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportPause", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    edit->getTransport().stop(false, false);
                    DBG("Transport: Paused (Native)");
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportStop", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    edit->getTransport().stop(false, false);
                    edit->getTransport().setPosition(te::TimePosition::fromSeconds(0.0));
                    DBG("Transport: Stopped & Rewound (Native)");
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportSeek", [this](auto& args, auto complete) {
            if (args.size() > 0) {
                double pos = static_cast<double>(args[0]);
                juce::MessageManager::callAsync([this, pos]() {
                    if (edit) {
                        edit->getTransport().setPosition(te::TimePosition::fromSeconds(pos));
                        DBG("Transport: Seeked to " + juce::String(pos) + " (Native)");
                    }
                });
            }
            complete("ok");
        })
        .withNativeFunction("setLoopRange", [this](auto& args, auto complete) {
            if (!edit || args.size() < 2) { complete("ok"); return; }
            int startBar = static_cast<int>(args[0]);
            int endBar   = static_cast<int>(args[1]);
            juce::MessageManager::callAsync([this, startBar, endBar]() {
                if (!edit) return;
                double bpm = edit->tempoSequence.getTempos()[0]->getBpm();
                double secPerBar = (60.0 / bpm) * 4.0;
                auto startTime = te::TimePosition::fromSeconds(secPerBar * startBar);
                auto endTime   = te::TimePosition::fromSeconds(secPerBar * endBar);
                edit->getTransport().setLoopRange(te::TimeRange(startTime, endTime));
                DBG("Transport: Loop range set to bar " + juce::String(startBar) + " - " + juce::String(endBar));
            });
            complete("ok");
        })

        // Export to MIDI (Sheet Music)
        .withNativeFunction("exportSheetMusic", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this]() {
                exportSheetMusic();
            });
            complete("ok");
        })
        // Export Stems
        .withNativeFunction("exportStems", [this](auto& args, auto complete) {
            bool includeReturnFx = false;
            if (args.size() > 0) {
                includeReturnFx = static_cast<bool>(args[0]);
            }
            juce::MessageManager::callAsync([this, includeReturnFx]() {
                exportStems(includeReturnFx);
            });
            complete("ok");
        })
        // Export Master (full mix)
        .withNativeFunction("exportMaster", [this](auto& args, auto complete) {
            juce::MessageManager::callAsync([this]() {
                exportMaster();
            });
            complete("ok");
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
