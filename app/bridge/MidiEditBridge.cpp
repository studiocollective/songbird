#include "SongbirdEditor.h"

//==============================================================================
// Bridge: MIDI note editing (piano roll)
//==============================================================================

void SongbirdEditor::registerMidiEditBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // ====================================================================
        // MIDI Note Editing (Piano Roll Editor)
        // ====================================================================
        //
        // Individual note operations: each modifies a single note in the
        // Tracktion MIDI clip (instant on message thread). Bird file write
        // and trackState emit are debounced via scheduleMidiCommit().

        // --- Add a MIDI note ---
        // Args: (trackId, sectionName, pitch, beat, duration, velocity)
        //   beat is relative to section start, in beats (0-based)
        .withNativeFunction("midiAddNote", [this](auto& args, auto complete) {
            if (!edit || !currentBirdFile.existsAsFile() || args.size() < 6) {
                complete("{\"success\":false}"); return;
            }
            int trackId          = static_cast<int>(args[0]);
            int secIndex         = static_cast<int>(args[1]);
            int pitch            = static_cast<int>(args[2]);
            double beat          = static_cast<double>(args[3]);
            double duration      = static_cast<double>(args[4]);
            int velocity         = static_cast<int>(args[5]);

            juce::MessageManager::callAsync([this, trackId, secIndex, pitch, beat, duration, velocity]() {
                double secOffset = 0.0;
                int secBars = 4;
                juce::String secName = "intro";
                for (int i = 0; i < (int)lastParseResult.arrangement.size(); i++) {
                    if (i == secIndex) { secBars = lastParseResult.arrangement[i].bars; secName = juce::String(lastParseResult.arrangement[i].sectionName); break; }
                    secOffset += lastParseResult.arrangement[i].bars * 4.0;
                }

                // Add note to Tracktion clip (instant)
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)audioTracks.size()) {
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTracks[trackId]->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                    if (mc) {
                        mc->getSequence().addNote(pitch,
                            te::BeatPosition::fromBeats(secOffset + beat),
                            te::BeatDuration::fromBeats(duration),
                            velocity, 0, nullptr);
                    }
                }

                // Debounce: bird write + trackState + commit happen after edits settle
                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })

        // --- Remove a MIDI note ---
        // Args: (trackId, sectionName, pitch, beat)
        .withNativeFunction("midiRemoveNote", [this](auto& args, auto complete) {
            if (!edit || !currentBirdFile.existsAsFile() || args.size() < 4) {
                complete("{\"success\":false}"); return;
            }
            int trackId          = static_cast<int>(args[0]);
            int secIndex         = static_cast<int>(args[1]);
            int pitch            = static_cast<int>(args[2]);
            double beat          = static_cast<double>(args[3]);

            juce::MessageManager::callAsync([this, trackId, secIndex, pitch, beat]() {
                double secOffset = 0.0;
                int secBars = 4;
                juce::String secName = "intro";
                for (int i = 0; i < (int)lastParseResult.arrangement.size(); i++) {
                    if (i == secIndex) { secBars = lastParseResult.arrangement[i].bars; secName = juce::String(lastParseResult.arrangement[i].sectionName); break; }
                    secOffset += lastParseResult.arrangement[i].bars * 4.0;
                }

                double absBeat = secOffset + beat;
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)audioTracks.size()) {
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTracks[trackId]->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                    if (mc) {
                        auto& seq = mc->getSequence();
                        for (int i = seq.getNumNotes() - 1; i >= 0; --i) {
                            auto* note = seq.getNote(i);
                            if (note->getNoteNumber() == pitch &&
                                std::abs(note->getStartBeat().inBeats() - absBeat) < 0.05) {
                                seq.removeNote(*note, nullptr);
                                break;
                            }
                        }
                    }
                }

                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })

        // --- Move / resize a MIDI note ---
        // Args: (trackId, sectionName, oldPitch, oldBeat, newPitch, newBeat, newDuration)
        .withNativeFunction("midiMoveNote", [this](auto& args, auto complete) {
            if (!edit || !currentBirdFile.existsAsFile() || args.size() < 7) {
                complete("{\"success\":false}"); return;
            }
            int trackId          = static_cast<int>(args[0]);
            int secIndex         = static_cast<int>(args[1]);
            int oldPitch         = static_cast<int>(args[2]);
            double oldBeat       = static_cast<double>(args[3]);
            int newPitch         = static_cast<int>(args[4]);
            double newBeat       = static_cast<double>(args[5]);
            double newDuration   = static_cast<double>(args[6]);

            juce::MessageManager::callAsync([this, trackId, secIndex, oldPitch, oldBeat, newPitch, newBeat, newDuration]() {
                double secOffset = 0.0;
                int secBars = 4;
                juce::String secName = "intro";
                for (int i = 0; i < (int)lastParseResult.arrangement.size(); i++) {
                    if (i == secIndex) { secBars = lastParseResult.arrangement[i].bars; secName = juce::String(lastParseResult.arrangement[i].sectionName); break; }
                    secOffset += lastParseResult.arrangement[i].bars * 4.0;
                }

                double absOld = secOffset + oldBeat;
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId >= 0 && trackId < (int)audioTracks.size()) {
                    te::MidiClip* mc = nullptr;
                    for (auto* clip : audioTracks[trackId]->getClips())
                        if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                    if (mc) {
                        auto& seq = mc->getSequence();
                        int vel = 100;
                        for (int i = seq.getNumNotes() - 1; i >= 0; --i) {
                            auto* note = seq.getNote(i);
                            if (note->getNoteNumber() == oldPitch &&
                                std::abs(note->getStartBeat().inBeats() - absOld) < 0.05) {
                                vel = note->getVelocity();
                                seq.removeNote(*note, nullptr);
                                break;
                            }
                        }
                        seq.addNote(newPitch,
                            te::BeatPosition::fromBeats(secOffset + newBeat),
                            te::BeatDuration::fromBeats(newDuration),
                            vel, 0, nullptr);
                    }
                }

                pendingMidiEdit = { trackId, secName, secOffset, secBars };
                scheduleMidiCommit();
            });
            complete("{\"success\":true}");
        })
        // --- Set loop length on a MIDI clip ---
        // Args: (trackId, loopBeats)  — 0 = disable looping
        .withNativeFunction("midiSetLoopLength", [this](auto& args, auto complete) {
            if (!edit || args.size() < 2) {
                complete("{\"success\":false}"); return;
            }
            int trackId       = static_cast<int>(args[0]);
            double loopBeats  = static_cast<double>(args[1]);

            juce::MessageManager::callAsync([this, trackId, loopBeats]() {
                auto audioTracks = te::getAudioTracks(*edit);
                if (trackId < 0 || trackId >= (int)audioTracks.size()) return;

                te::MidiClip* mc = nullptr;
                for (auto* clip : audioTracks[trackId]->getClips())
                    if (auto* m = dynamic_cast<te::MidiClip*>(clip)) { mc = m; break; }
                if (!mc) return;

                if (loopBeats > 0) {
                    // Remove notes beyond the loop boundary
                    auto& seq = mc->getSequence();
                    for (int i = seq.getNumNotes() - 1; i >= 0; --i) {
                        auto* note = seq.getNote(i);
                        if (note->getStartBeat().inBeats() >= loopBeats)
                            seq.removeNote(*note, nullptr);
                    }
                    mc->setLoopRangeBeats(te::BeatRange(
                        te::BeatPosition(),
                        te::BeatDuration::fromBeats(loopBeats)));
                    mc->loopedSequenceType = te::MidiClip::LoopedSequenceType::loopRangeDefinesAllRepetitions;
                    DBG("midiSetLoopLength: track " + juce::String(trackId) + " loop = " + juce::String(loopBeats) + " beats");
                } else {
                    mc->disableLooping();
                    DBG("midiSetLoopLength: track " + juce::String(trackId) + " looping disabled");
                }

                // Emit lightweight notesChanged (notes may have been trimmed at loop boundary)
                if (webView) {
                    juce::String notesJson = "{\"trackId\":" + juce::String(trackId) + ",\"loopLengthBeats\":" + juce::String(loopBeats > 0 ? loopBeats : 0) + ",\"notes\":[";
                    if (mc) {
                        auto& seq2 = mc->getSequence();
                        for (int n = 0; n < seq2.getNumNotes(); n++) {
                            auto* note = seq2.getNote(n);
                            if (n > 0) notesJson += ",";
                            notesJson += "{\"pitch\":" + juce::String(note->getNoteNumber()) +
                                         ",\"beat\":" + juce::String(note->getStartBeat().inBeats(), 3) +
                                         ",\"duration\":" + juce::String(note->getLengthBeats().inBeats(), 3) +
                                         ",\"velocity\":" + juce::String(note->getVelocity()) + "}";
                        }
                    }
                    notesJson += "]}";
                    webView->emitEventIfBrowserIsVisible("notesChanged", juce::var(notesJson));
                }
            });
            complete("{\"success\":true}");
        });
}
