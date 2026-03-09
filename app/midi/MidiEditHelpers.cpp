#include "SongbirdEditor.h"

void SongbirdEditor::setSidechainSource(int destTrackId, int sourceTrackId)
{
    if (!edit) return;

    auto audioTracks = te::getAudioTracks(*edit);
    
    te::Track* destTrack = nullptr;
    if (destTrackId >= 0 && destTrackId < audioTracks.size())
        destTrack = audioTracks[destTrackId];

    if (!destTrack) return;

    // Let's use bus #1 as the dedicated SC bus for now
    const int SC_BUS = 1;

    // Helper to auto-set parameters
    auto autoSetParam = [](te::Plugin* plugin, const juce::String& nameSubstr, float normValue)
    {
        for (auto* param : plugin->getAutomatableParameters())
        {
            if (param->getParameterName().containsIgnoreCase(nameSubstr))
            {
                param->setNormalisedParameter(normValue, juce::sendNotification);
                DBG("SC Auto-set: " + param->getParameterName() + " → " + juce::String(normValue));
                return;
            }
        }
        DBG("SC Auto-set: no param matching '" + nameSubstr + "' found (plugin=" + plugin->getName() + ")");
    };

    bool enabling = (sourceTrackId >= 0);
    for (auto* plugin : destTrack->pluginList)
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin)) continue;
        if (dynamic_cast<te::LevelMeterPlugin*>(plugin))   continue;
        if (dynamic_cast<te::AuxSendPlugin*>(plugin))      continue;
        if (dynamic_cast<te::AuxReturnPlugin*>(plugin))    continue;

        // Exact parameter names from: ScanPluginParams "/Library/Audio/Plug-Ins/VST3/Console 1.vst3"
        // Index 24:  "Compressor"                  – 2 steps  (0=off, 1=on)
        // Index 31:  "External Sidechain"           – 3 steps  (0=Int, 0.5=Ext 1, 1.0=Ext 2)
        // Index 119: "Ext. Sidechain to Subsystem"  – 2 steps  (0=off, 1=on)
        // Index 29:  "Compression"                  – continuous amount/threshold proxy
        autoSetParam(plugin, "Compressor",                 enabling ? 1.0f : 0.0f);
        autoSetParam(plugin, "External Sidechain",         enabling ? 0.5f : 0.0f); // 0.5 = Ext 1
        autoSetParam(plugin, "Ext. Sidechain to Subsystem", enabling ? 1.0f : 0.0f);

        if (enabling)
        {
            // Default starting compression amount — user can adjust via sensitivity slider
            autoSetParam(plugin, "Compression", 0.4f);
        }
    }

    DBG("Sidechain: Track " + juce::String(sourceTrackId)
        + " -> Track " + juce::String(destTrackId)
        + " via bus " + juce::String(SC_BUS));
}

// ====================================================================
// MIDI editing helpers (piano roll)
// ====================================================================

void SongbirdEditor::emitTrackState(bool emitLoadingDone)
{
    if (!webView || !edit) return;

    juce::MessageManager::getInstance()->callAsync([this, emitLoadingDone]() {
        if (!webView || !edit) return;

        // Step 1: Build complete trackState as a var, then strip notes for metadata-only emit
        auto fullJsonStr = getTrackStateJSON();
        auto parsed = juce::JSON::parse(fullJsonStr);
        
        // Strip notes from each track (we'll send them per-track via notesChanged)
        if (auto* tracks = parsed.getProperty("tracks", {}).getArray()) {
            for (auto& t : *tracks) {
                if (auto* obj = t.getDynamicObject())
                    obj->setProperty("notes", juce::Array<juce::var>());
            }
        }

        webView->emitEventIfBrowserIsVisible("trackState", parsed);

        // Step 2: Emit per-track notes via notesChanged after a delay.
        juce::Timer::callAfterDelay(100, [this, emitLoadingDone]() {
            if (!webView || !edit) return;

            auto audioTracks = te::getAudioTracks(*edit);
            for (int t = 0; t < audioTracks.size(); t++) {
                auto* audioTrack = audioTracks[t];
                if (!audioTrack) continue;

                juce::DynamicObject::Ptr notesObj = new juce::DynamicObject();
                notesObj->setProperty("trackId", t);

                double loopLen = 0;
                te::MidiClip* mc = nullptr;
                for (auto* clip : audioTrack->getClips()) {
                    if (auto* m = dynamic_cast<te::MidiClip*>(clip)) {
                        mc = m;
                        if (m->isLooping()) loopLen = m->getLoopLengthBeats().inBeats();
                        break;
                    }
                }
                notesObj->setProperty("loopLengthBeats", loopLen);

                juce::Array<juce::var> notesArray;
                if (mc) {
                    auto& seq = mc->getSequence();
                    for (int n = 0; n < seq.getNumNotes(); n++) {
                        auto* note = seq.getNote(n);
                        juce::DynamicObject::Ptr noteObj = new juce::DynamicObject();
                        noteObj->setProperty("pitch", note->getNoteNumber());
                        noteObj->setProperty("beat", note->getStartBeat().inBeats());
                        noteObj->setProperty("duration", note->getLengthBeats().inBeats());
                        noteObj->setProperty("velocity", note->getVelocity());
                        notesArray.add(juce::var(noteObj.get()));
                    }
                }
                notesObj->setProperty("notes", notesArray);

                webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesObj.get()));
            }

            if (emitLoadingDone) {
                juce::DynamicObject::Ptr doneObj = new juce::DynamicObject();
                doneObj->setProperty("message", "done");
                doneObj->setProperty("progress", 1.0);
                webView->emitEventIfBrowserIsVisible("loadingProgress", juce::var(doneObj.get()));
            }
        });
    });
}

void SongbirdEditor::scheduleMidiCommit()
{
    midiEditPending = true;
    dirtyPluginParams.clear();
    dirtyPluginParams["MIDI Edit"].insert("edit notes");
    pluginParamsDirty = true;
    startTimer(800);
}

std::vector<SongbirdEditor::ClipNote> SongbirdEditor::collectNotesFromClip(
    int trackId, double secOffset, int secBars)
{
    // MUST run on message thread — reads from Tracktion MidiSequence
    std::vector<ClipNote> result;
    if (!edit) return result;

    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId < 0 || trackId >= (int)audioTracks.size()) return result;

    te::MidiClip* mc = nullptr;
    for (auto* clip : audioTracks[trackId]->getClips())
        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
    if (!mc) return result;

    auto& seq = mc->getSequence();
    double secEnd = secOffset + secBars * 4.0;

    for (int i = 0; i < seq.getNumNotes(); ++i) {
        auto* note = seq.getNote(i);
        double b = note->getStartBeat().inBeats();
        if (b >= secOffset - 0.01 && b < secEnd + 0.01) {
            result.push_back({ note->getNoteNumber(), note->getVelocity(), b - secOffset });
        }
    }
    return result;
}

void SongbirdEditor::writeBirdFromClip(int trackId, const juce::String& sectionName,
                                        double /*secOffset*/, int secBars,
                                        const std::vector<ClipNote>& clipNotes)
{
    // Safe to run on background thread — only does string manipulation + file I/O.
    // No Tracktion or JUCE ValueTree access.
    if (!currentBirdFile.existsAsFile()) return;
    if (trackId < 0 || trackId >= (int)lastParseResult.channels.size()) return;

    auto& chInfo = lastParseResult.channels[trackId];
    juce::String channelName = chInfo.name;
    int channelNumInFile = chInfo.channel + 1;

    // Convert notes to step map
    struct StepNote { int pitch; int velocity; };
    int totalSteps = secBars * 16;
    std::map<int, std::vector<StepNote>> stepMap;

    for (auto& cn : clipNotes) {
        int step = static_cast<int>(std::round(cn.relBeat * 4.0));
        if (step >= 0 && step < totalSteps)
            stepMap[step].push_back({ cn.pitch, cn.velocity });
    }

    // Build p/v/n lines
    int maxVoices = 0;
    for (auto& [step, notes] : stepMap)
        maxVoices = std::max(maxVoices, (int)notes.size());
    if (maxVoices == 0) maxVoices = 1;

    for (auto& [step, notes] : stepMap)
        std::sort(notes.begin(), notes.end(),
            [](const StepNote& a, const StepNote& b) { return a.pitch > b.pitch; });

    juce::String pLine = "    p";
    juce::String vLine = "      v";
    std::vector<juce::String> nLines(maxVoices, "        n");

    for (int s = 0; s < totalSteps; s++) {
        auto it = stepMap.find(s);
        if (it != stepMap.end() && !it->second.empty()) {
            pLine += " x";
            vLine += " " + juce::String(it->second[0].velocity);
            for (int v = 0; v < maxVoices; v++) {
                if (v < (int)it->second.size())
                    nLines[v] += " " + juce::String(it->second[v].pitch);
                else
                    nLines[v] += " -";
            }
        } else {
            pLine += " _";
        }
    }

    juce::String newBlock = pLine + "\n" + vLine + "\n";
    for (auto& nl : nLines)
        newBlock += nl + "\n";

    // --- Replace the channel block in the bird file ---
    auto birdText = currentBirdFile.loadFileAsString();
    auto lines = juce::StringArray::fromLines(birdText);

    int secStart = -1, secEndLine = lines.size();
    juce::String secMarker = "sec " + sectionName;
    for (int i = 0; i < lines.size(); i++) {
        auto trimmed = lines[i].trim();
        if (trimmed == secMarker || trimmed.startsWith(secMarker + " ")) {
            secStart = i;
            for (int j = i + 1; j < lines.size(); j++) {
                auto t = lines[j].trim();
                if (t.startsWith("sec ") && !t.startsWith(secMarker)) { secEndLine = j; break; }
            }
            break;
        }
    }
    if (secStart < 0) return;

    juce::String chMarker = "ch " + juce::String(channelNumInFile) + " " + channelName;
    int chStart = -1, chEnd = secEndLine;
    for (int i = secStart + 1; i < secEndLine; i++) {
        auto trimmed = lines[i].trim();
        if (trimmed == chMarker || trimmed.startsWith(chMarker + " ")) {
            chStart = i;
            for (int j = i + 1; j < secEndLine; j++) {
                if (lines[j].trim().startsWith("ch ")) { chEnd = j; break; }
            }
            break;
        }
    }

    if (chStart < 0) {
        lines.insert(secEndLine, "  " + chMarker + "\n" + newBlock);
    } else {
        juce::StringArray preserved;
        for (int i = chStart + 1; i < chEnd; i++) {
            auto t = lines[i].trim();
            if (!t.startsWith("p ") && !t.startsWith("v ") && !t.startsWith("n "))
                preserved.add(lines[i]);
        }
        lines.removeRange(chStart + 1, chEnd - chStart - 1);
        int insertAt = chStart + 1;
        auto newLines = juce::StringArray::fromLines(newBlock);
        if (newLines.size() > 0 && newLines[newLines.size() - 1].isEmpty())
            newLines.remove(newLines.size() - 1);
        for (int i = 0; i < newLines.size(); i++)
            lines.insert(insertAt + i, newLines[i]);
        insertAt += newLines.size();
        for (int i = 0; i < preserved.size(); i++)
            lines.insert(insertAt + i, preserved[i]);
    }

    currentBirdFile.replaceWithText(lines.joinIntoString("\n"));
    
    // Notify frontend that the raw .bird file on disk has changed
    // so the Bird File Viewer can refresh its contents
    if (webView) {
        juce::MessageManager::callAsync([this]() {
            if (webView)
                webView->emitEventIfBrowserIsVisible("birdContentChanged", juce::var());
        });
    }

    DBG("MidiEdit: writeBirdFromClip sec=" + sectionName + " ch=" + channelName);
}
