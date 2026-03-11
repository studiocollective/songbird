#include "SongbirdEditor.h"

void SongbirdEditor::exportSheetMusic()
{
    if (currentBirdFile == juce::File() || lastParseResult.channels.empty()) {
        logToJS("Export error: No valid bird file loaded.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Sheet Music (MIDI)",
        currentBirdFile.getParentDirectory().getChildFile(currentBirdFile.getFileNameWithoutExtension() + ".mid"),
        "*.mid"
    );

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File()) {
                juce::MidiFile midiFile;
                midiFile.setTicksPerQuarterNote(960);
                
                for (const auto& ch : lastParseResult.channels) {
                    juce::MidiMessageSequence seq;
                    
                    // Key signature meta event
                    if (lastParseResult.hasKeySignature) {
                        seq.addEvent(juce::MidiMessage::keySignatureMetaEvent(lastParseResult.keySharpsFlats, lastParseResult.keyIsMinor), 0.0);
                    }

                    // Track name meta event (type 3)
                    seq.addEvent(juce::MidiMessage::textMetaEvent(3, juce::String(ch.name)), 0.0);
                    
                    // Channel number (1-16)
                    int midiChannel = juce::jlimit(1, 16, ch.channel + 1);

                    for (const auto& note : ch.notes) {
                        double startTick = note.beatPos * 960.0;
                        double endTick = (note.beatPos + note.duration) * 960.0;
                        
                        auto noteOn = juce::MidiMessage::noteOn(midiChannel, note.pitch, (juce::uint8)note.velocity);
                        auto noteOff = juce::MidiMessage::noteOff(midiChannel, note.pitch, (juce::uint8)0);
                        
                        seq.addEvent(noteOn, startTick);
                        seq.addEvent(noteOff, endTick);
                    }
                    
                    seq.updateMatchedPairs();
                    midiFile.addTrack(seq);
                }
                
                juce::FileOutputStream stream(file);
                if (stream.openedOk()) {
                    stream.setPosition(0);
                    stream.truncate();
                    midiFile.writeTo(stream);
                    logToJS("Successfully exported MIDI to: " + file.getFullPathName());
                } else {
                    logToJS("Failed to open file for writing: " + file.getFullPathName());
                }
            }
        });
}

juce::String SongbirdEditor::getTrackStateJSON()
{
    if (!edit) return "{}";
    auto json = BirdLoader::getTrackStateJSON(*edit, &lastParseResult);

    // Inject audioSource per track from AudioRecorder state
    if (audioRecorder)
    {
        auto parsed = juce::JSON::parse(json);
        if (auto* tracks = parsed.getProperty("tracks", {}).getArray())
        {
            for (auto& t : *tracks)
            {
                if (auto* obj = t.getDynamicObject())
                {
                    int id = (int)obj->getProperty("id");
                    if (auto* info = audioRecorder->findTrackInfo(id))
                    {
                        auto* src = new juce::DynamicObject();
                        src->setProperty("type", info->sourceType == AudioRecorder::SourceType::HardwareInput ? "hardware" : "loopback");
                        if (info->sourceType == AudioRecorder::SourceType::HardwareInput)
                            src->setProperty("deviceName", info->sourceName);
                        else
                            src->setProperty("sourceTrackId", info->sourceTrackId);
                        obj->setProperty("audioSource", juce::var(src));
                    }
                }
            }
        }
        json = juce::JSON::toString(parsed);
    }

    return json;
}

void SongbirdEditor::exportStems(bool includeReturnFx)
{
    if (!edit || currentBirdFile == juce::File()) {
        logToJS("Export error: No valid edit loaded.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Stems",
        currentBirdFile.getParentDirectory(),
        ""
    );

    chooser->launchAsync(juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::openMode,
        [this, chooser, includeReturnFx](const juce::FileChooser& fc) {
            auto destDir = fc.getResult();
            if (destDir == juce::File() || !destDir.isDirectory()) return;

            auto& transport = edit->getTransport();
            te::TimeRange range = transport.looping
                ? transport.getLoopRange()
                : te::TimeRange(te::TimePosition::fromSeconds(0.0), edit->getLength());

            auto allTracks = te::getAllTracks(*edit);

            juce::Array<te::Track*> tracksToProcess;
            for (auto* t : allTracks)
                if (t->isAudioTrack() && !t->isMasterTrack() && !t->isFolderTrack())
                    tracksToProcess.add(t);

            if (tracksToProcess.isEmpty()) {
                logToJS("Export error: No audio tracks to export.");
                return;
            }

            // Build full bitset (all tracks) for wet mode
            juce::BigInteger fullBitset;
            for (int i = 0; i < allTracks.size(); ++i)
                fullBitset.setBit(i);

            int total = tracksToProcess.size();
            if (webView) {
                juce::String startJson = "{\"current\":0,\"total\":" + juce::String(total) + ",\"name\":\"\"}";
                webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(startJson));
            }

            struct RenderJob : public juce::Thread
            {
                RenderJob(SongbirdEditor& editor_,
                          te::Edit& edit_,
                          juce::Array<te::Track*> tracksToProcess_,
                          juce::Array<te::Track*> allTracks_,
                          te::TimeRange range_,
                          juce::File destDir_,
                          bool includeReturnFx_,
                          juce::BigInteger fullBitset_)
                    : juce::Thread("StemExport"),
                      editor(editor_), edit(edit_),
                      tracksToProcess(tracksToProcess_),
                      allTracks(allTracks_),
                      range(range_), destDir(destDir_),
                      includeReturnFx(includeReturnFx_),
                      fullBitset(fullBitset_)
                {}

                void run() override
                {
                    int total = tracksToProcess.size();
                    for (int i = 0; i < total; ++i)
                    {
                        if (threadShouldExit()) break;

                        auto* track = tracksToProcess[i];
                        juce::String trackName = track->getName()
                            .replaceCharacter(' ', '_')
                            .replaceCharacter('/', '_')
                            .replaceCharacter('\\', '_');

                        juce::File outputFile = destDir.getChildFile(trackName + ".wav");

                        // Emit "starting" BEFORE the render so bar shows partial progress
                        // while the render is in flight (current = i, not i+1)
                        juce::MessageManager::callAsync([this, i, total, trackName]() {
                            if (editor.webView) {
                                juce::String json = "{\"current\":" + juce::String(i)
                                    + ",\"total\":" + juce::String(total)
                                    + ",\"name\":\"" + trackName + "\"}";
                                editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                            }
                        });

                        if (includeReturnFx) {
                            // Mute all OTHER tracks so only the target's audio flows through
                            // the master chain (and aux returns).
                            // Use mute — NOT solo — because the offline renderer respects
                            // isMuted() but ignores isSolo().
                            juce::Array<bool> muteWas;
                            for (auto* t : tracksToProcess)
                                if (auto* at = dynamic_cast<te::AudioTrack*>(t))
                                    muteWas.add(at->isMuted(false));

                            for (auto* t : tracksToProcess)
                                if (auto* at = dynamic_cast<te::AudioTrack*>(t))
                                    at->setMute(t != track); // mute everything except target

                            te::Renderer::renderToFile("Exporting " + trackName, outputFile,
                                                       edit, range, fullBitset, true, true, {}, false);

                            int idx = 0;
                            for (auto* t : tracksToProcess)
                                if (auto* at = dynamic_cast<te::AudioTrack*>(t))
                                    at->setMute(muteWas[idx++]);
                        } else {
                            // Dry: render only this track's chain directly, no master
                            juce::BigInteger bitset;
                            int trackIdx = allTracks.indexOf(track);
                            if (trackIdx >= 0)
                                bitset.setBit(trackIdx);

                            te::Renderer::renderToFile("Exporting " + trackName, outputFile,
                                                       edit, range, bitset, true, true, {}, false);
                        }

                        // Emit "done" AFTER the render
                        juce::MessageManager::callAsync([this, i, total, trackName]() {
                            if (editor.webView) {
                                juce::String json = "{\"current\":" + juce::String(i + 1)
                                    + ",\"total\":" + juce::String(total)
                                    + ",\"name\":\"" + trackName + "\"}";
                                editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                            }
                        });
                    }

                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView)
                            editor.webView->emitEventIfBrowserIsVisible("exportDone", juce::var("null"));
                        editor.logToJS("Successfully exported stems to: " + destDir.getFullPathName());
                        editor.stemExportThread = nullptr;
                    });
                }

                SongbirdEditor& editor;
                te::Edit& edit;
                juce::Array<te::Track*> tracksToProcess;
                juce::Array<te::Track*> allTracks;
                te::TimeRange range;
                juce::File destDir;
                bool includeReturnFx;
                juce::BigInteger fullBitset;
            };

            stemExportThread = std::make_unique<RenderJob>(
                *this, *edit, tracksToProcess, allTracks, range, destDir, includeReturnFx, fullBitset);
            stemExportThread->startThread();
        });
}

void SongbirdEditor::exportMaster()
{
    if (!edit || currentBirdFile == juce::File()) {
        logToJS("Export error: No valid edit loaded.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export Master",
        currentBirdFile.getParentDirectory(),
        "*.wav"
    );

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& fc) {
            auto outputFile = fc.getResult();
            if (outputFile == juce::File()) return;
            if (!outputFile.hasFileExtension("wav"))
                outputFile = outputFile.withFileExtension("wav");

            auto& transport = edit->getTransport();
            te::TimeRange range = transport.looping
                ? transport.getLoopRange()
                : te::TimeRange(te::TimePosition::fromSeconds(0.0), edit->getLength());

            auto allTracks = te::getAllTracks(*edit);
            juce::BigInteger fullBitset;
            for (int i = 0; i < allTracks.size(); ++i)
                fullBitset.setBit(i);

            if (webView) {
                juce::String startJson = "{\"current\":0,\"total\":1,\"name\":\"Master\"}";
                webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(startJson));
            }

            struct MasterRenderJob : public juce::Thread
            {
                MasterRenderJob(SongbirdEditor& editor_, te::Edit& edit_,
                                te::TimeRange range_, juce::File outputFile_,
                                juce::BigInteger bitset_)
                    : juce::Thread("MasterExport"),
                      editor(editor_), edit(edit_),
                      range(range_), outputFile(outputFile_), bitset(bitset_)
                {}

                void run() override
                {
                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView) {
                            juce::String json = "{\"current\":0,\"total\":1,\"name\":\"Master\"}";
                            editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                        }
                    });

                    te::Renderer::renderToFile("Exporting Master", outputFile,
                                               edit, range, bitset, true, true, {}, false);

                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView) {
                            juce::String json = "{\"current\":1,\"total\":1,\"name\":\"Master\"}";
                            editor.webView->emitEventIfBrowserIsVisible("exportProgress", juce::var(json));
                        }
                    });

                    juce::MessageManager::callAsync([this]() {
                        if (editor.webView)
                            editor.webView->emitEventIfBrowserIsVisible("exportDone", juce::var("null"));
                        editor.logToJS("Successfully exported master to: " + outputFile.getFullPathName());
                        editor.stemExportThread = nullptr;
                    });
                }

                SongbirdEditor& editor;
                te::Edit& edit;
                te::TimeRange range;
                juce::File outputFile;
                juce::BigInteger bitset;
            };

            stemExportThread = std::make_unique<MasterRenderJob>(
                *this, *edit, range, outputFile, fullBitset);
            stemExportThread->startThread();
        });
}
