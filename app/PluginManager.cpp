#include "SongbirdEditor.h"

//==============================================================================
// Plugin window management
//==============================================================================

void SongbirdEditor::openPluginWindow(int trackId, const juce::String& slotType, const juce::String& pluginId)
{
    if (!edit) return;

    auto audioTracks = te::getAudioTracks(*edit);
    if (trackId < 1 || trackId > audioTracks.size()) return;

    auto* track = audioTracks[trackId - 1];
    if (!track) return;

    te::Plugin::Ptr targetPlugin;

    if (slotType == "instrument")
    {
        // Instrument is the first non-volume plugin on a MIDI track
        for (auto* plugin : track->pluginList)
        {
            if (plugin != track->getVolumePlugin())
            {
                targetPlugin = plugin;
                break;
            }
        }
    }
    else if (slotType == "channelStrip")
    {
        // Channel strip — look for external plugins (not the built-in volume/pan)
        for (auto* plugin : track->pluginList)
        {
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin))
            {
                targetPlugin = ext;
                break;
            }
        }
    }

    if (targetPlugin)
    {
        targetPlugin->showWindowExplicitly();
        DBG("Opened plugin window: " + targetPlugin->getName() + " on track " + juce::String(trackId));
    }
    else
    {
        DBG("No plugin found for slot " + slotType + " on track " + juce::String(trackId));
    }
}
