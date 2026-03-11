#include "SongbirdEditor.h"
#include "MasterAnalyzerPlugin.h"

//==============================================================================
// Bridge: Transport controls and export
//==============================================================================

void SongbirdEditor::registerTransportBridge(juce::WebBrowserComponent::Options& options)
{
    options = std::move(options)
        // Explicit Transport Controls
        .withNativeFunction("transportPlay", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    // Set up fade-in BEFORE play so the first buffer is already ramping
                    if (auto* analyzer = getAnalyzerPlugin())
                        analyzer->requestFadeIn();
                    edit->getTransport().play(false);
                    DBG("Transport: Playing (Native)");
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportPause", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    // Start fade-out, then stop after ramp completes (~6ms)
                    if (auto* analyzer = getAnalyzerPlugin())
                        analyzer->requestFadeOut();
                    juce::Timer::callAfterDelay(8, [this]() {
                        if (edit) {
                            edit->getTransport().stop(false, false);
                            // Reset to idle so next play starts clean
                            if (auto* analyzer = getAnalyzerPlugin())
                                analyzer->goIdle();
                            DBG("Transport: Paused after fade-out (Native)");
                        }
                    });
                }
            });
            complete("ok");
        })
        .withNativeFunction("transportStop", [this](auto&, auto complete) {
            juce::MessageManager::callAsync([this]() {
                if (edit) {
                    // Start fade-out, then stop + rewind after ramp completes
                    if (auto* analyzer = getAnalyzerPlugin())
                        analyzer->requestFadeOut();
                    juce::Timer::callAfterDelay(8, [this]() {
                        if (edit) {
                            edit->getTransport().stop(false, false);
                            edit->getTransport().setPosition(te::TimePosition::fromSeconds(0.0));
                            if (auto* analyzer = getAnalyzerPlugin())
                                analyzer->goIdle();
                            DBG("Transport: Stopped & Rewound after fade-out (Native)");
                        }
                    });
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
        });
}
