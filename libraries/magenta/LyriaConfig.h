#pragma once

#include "MagentaAliases.h"

namespace magenta {

struct Prompt {
  string id;
  string text = "";
  float weight = 0.5;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Prompt, id, text, weight);

struct LyriaConfig {
  float temperature = 1.0f;
  float guidance = 3.0f;
  int topk = 250;
  int bpm = 120;
  bool useBpm = false;
  float density = 0.5f;
  bool useDensity = false;
  float brightness = 0.5f;
  bool useBrightness = false;
  int rootNote = 0;  // Scale enum index
  bool useScale = false;
  bool muteBass = false;
  bool muteDrums = false;
  bool muteOther = false;
  string generationQuality = "quality";
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    LyriaConfig, temperature, guidance, topk, bpm, useBpm,
    density, useDensity, brightness, useBrightness,
    rootNote, useScale, muteBass, muteDrums, muteOther,
    generationQuality);

}  // namespace magenta
