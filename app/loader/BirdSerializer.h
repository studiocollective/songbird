#pragma once

#include "BirdLoader.h"

// Serialize all track note data from an Edit as a JSON string for the UI.
juce::String getTrackStateJSON(te::Edit& edit, const BirdParseResult* parseResult = nullptr);
