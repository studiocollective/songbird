#pragma once

#include "MagentaAliases.h"

namespace magenta {

constexpr auto GEMINI_MODEL = "models/lyria-realtime-exp";
constexpr auto GEMINI_HOST = "generativelanguage.googleapis.com";
constexpr auto GEMINI_URI_PREFIX = "wss://";
constexpr auto GEMINI_URI =
    "/ws/"
    "google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateMusic";
constexpr auto GEMINI_URI_PARAMS = "?key=";

constexpr auto SOURCE_SAMPLE_RATE = 48000.0;
constexpr auto DEFAULT_FRAME_CAPACITY =
    static_cast<size_t>(SOURCE_SAMPLE_RATE * 60 * 2);

// ApplicationProperties path — same as The Infinite Crate
constexpr auto MAGENTA_COMPANY_NAME = "Magenta";
constexpr auto MAGENTA_APP_NAME = "The Infinite Crate";
constexpr auto APP_STATE_KEY = "app-state";

}  // namespace magenta
