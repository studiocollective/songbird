#pragma once

#include "MagentaAliases.h"
#include "MagentaConstants.h"
#include "Optionals.h"

namespace magenta {

struct GeminiConfig {
  string endpoint;
  string audioModel;
  string apiKey;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(GeminiConfig, endpoint, audioModel, apiKey);

////////////////////////////////////////////////////////////
/// Gemini API Types
////////////////////////////////////////////////////////////

enum Scale {
  SCALE_UNSPECIFIED,
  C_MAJOR_A_MINOR,
  D_FLAT_MAJOR_B_FLAT_MINOR,
  D_MAJOR_B_MINOR,
  E_FLAT_MAJOR_C_MINOR,
  E_MAJOR_D_FLAT_MINOR,
  F_MAJOR_D_MINOR,
  G_FLAT_MAJOR_E_FLAT_MINOR,
  G_MAJOR_E_MINOR,
  A_FLAT_MAJOR_F_MINOR,
  A_MAJOR_G_FLAT_MINOR,
  B_FLAT_MAJOR_G_MINOR,
  B_MAJOR_A_FLAT_MINOR,
};
NLOHMANN_JSON_SERIALIZE_ENUM(Scale, {
    {SCALE_UNSPECIFIED, "SCALE_UNSPECIFIED"},
    {C_MAJOR_A_MINOR, "C_MAJOR_A_MINOR"},
    {D_FLAT_MAJOR_B_FLAT_MINOR, "D_FLAT_MAJOR_B_FLAT_MINOR"},
    {D_MAJOR_B_MINOR, "D_MAJOR_B_MINOR"},
    {E_FLAT_MAJOR_C_MINOR, "E_FLAT_MAJOR_C_MINOR"},
    {E_MAJOR_D_FLAT_MINOR, "E_MAJOR_D_FLAT_MINOR"},
    {F_MAJOR_D_MINOR, "F_MAJOR_D_MINOR"},
    {G_FLAT_MAJOR_E_FLAT_MINOR, "G_FLAT_MAJOR_E_FLAT_MINOR"},
    {G_MAJOR_E_MINOR, "G_MAJOR_E_MINOR"},
    {A_FLAT_MAJOR_F_MINOR, "A_FLAT_MAJOR_F_MINOR"},
    {A_MAJOR_G_FLAT_MINOR, "A_MAJOR_G_FLAT_MINOR"},
    {B_FLAT_MAJOR_G_MINOR, "B_FLAT_MAJOR_G_MINOR"},
    {B_MAJOR_A_FLAT_MINOR, "B_MAJOR_A_FLAT_MINOR"},
});

enum MusicGenerationMode {
  MUSIC_GENERATION_MODE_UNSPECIFIED,
  QUALITY,
  DIVERSITY,
};
NLOHMANN_JSON_SERIALIZE_ENUM(MusicGenerationMode, {
    {MUSIC_GENERATION_MODE_UNSPECIFIED, "MUSIC_GENERATION_MODE_UNSPECIFIED"},
    {QUALITY, "QUALITY"},
    {DIVERSITY, "DIVERSITY"},
});

enum LiveMusicPlaybackControl {
  PLAYBACK_CONTROL_UNSPECIFIED,
  PLAY,
  PAUSE,
  STOP,
  RESET_CONTEXT,
};
NLOHMANN_JSON_SERIALIZE_ENUM(LiveMusicPlaybackControl, {
    {PLAYBACK_CONTROL_UNSPECIFIED, "PLAYBACK_CONTROL_UNSPECIFIED"},
    {PLAY, "PLAY"},
    {PAUSE, "PAUSE"},
    {STOP, "STOP"},
    {RESET_CONTEXT, "RESET_CONTEXT"},
});

struct LiveMusicClientSetup {
  optional<string> model;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(LiveMusicClientSetup, model);

struct WeightedPrompt {
  optional<string> text;
  optional<float> weight;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(WeightedPrompt, text, weight);

struct LiveMusicClientContent {
  optional<vector<WeightedPrompt>> weightedPrompts;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(LiveMusicClientContent, weightedPrompts);

struct LiveMusicGenerationConfig {
  optional<float> temperature;
  optional<int> topK;
  optional<int> seed;
  optional<float> guidance;
  optional<int> bpm;
  optional<float> density;
  optional<float> brightness;
  optional<Scale> scale;
  optional<bool> muteBass;
  optional<bool> muteDrums;
  optional<bool> onlyBassAndDrums;
  optional<MusicGenerationMode> musicGenerationMode;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(LiveMusicGenerationConfig, temperature, topK, seed,
                            guidance, bpm, density, brightness, scale, muteBass,
                            muteDrums, onlyBassAndDrums, musicGenerationMode);

struct AudioChunk {
  optional<string> data;
  optional<string> mimeType;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(AudioChunk, data, mimeType);

struct LiveMusicServerContent {
  optional<vector<AudioChunk>> audioChunks;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(LiveMusicServerContent, audioChunks);

struct LiveMusicFilteredPrompt {
  optional<string> text;
  optional<string> filteredReason;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(LiveMusicFilteredPrompt, text, filteredReason);

////////////////////////////////////////////////////////////
/// Gemini Request Types
////////////////////////////////////////////////////////////

struct GeminiClientContent {
  vector<WeightedPrompt> weighted_prompts;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(GeminiClientContent, weighted_prompts);

struct GeminiSetupParams {
  LiveMusicClientSetup setup;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(GeminiSetupParams, setup);

struct GeminiPromptParams {
  GeminiClientContent client_content;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(GeminiPromptParams, client_content);

struct GeminiPlaybackParams {
  LiveMusicPlaybackControl playback_control;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(GeminiPlaybackParams, playback_control);

struct GeminiConfigParams {
  LiveMusicGenerationConfig music_generation_config;
};
NLOHMANN_DEFINE_TYPE_OPTIONAL(GeminiConfigParams, music_generation_config);

}  // namespace magenta
