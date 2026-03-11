#pragma once

#include "BirdLoader.h"
#include <functional>

// Populate a te::Edit from parsed bird data.
// Clears existing tracks and creates new ones from the parse result.
void populateEdit(te::Edit& edit, const BirdParseResult& result, te::Engine& engine,
                  std::function<void(const juce::String&, float)> progressCallback = nullptr);
