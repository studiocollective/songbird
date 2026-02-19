#include "Gemini.h"

namespace magenta {

Gemini::Gemini() {
  config = {
      .endpoint = GEMINI_URI,
      .audioModel = GEMINI_MODEL,
      .apiKey = "",
  };
  transport = "";
  lyriaConfig = "";
  prompts = "";

  geminiActive = false;

  ws = make_shared<WebSocket>();
  ws->onOpen = [this]() {
    DBG("GEMINI ++++++ Socket opened!");
    if (onConnected) onConnected();
    sendSetup();
  };
  ws->onMessage = [this](json payload) {
    geminiActive = true;
    if (onConnected) onConnected();
    parseMessage(payload);
  };
  ws->onClose = [this](json payload) {
    DBG("GEMINI ++++++ Socket closed!");
    geminiActive = false;
    if (onClose) onClose(payload);
  };
}

void Gemini::initialize(string apiKey) {
  config.apiKey = apiKey;
  DBG("API key set");
  connectWebSocket();
}

string Gemini::websocketUrl() {
  return string(GEMINI_URI_PREFIX) + string(GEMINI_HOST) + string(GEMINI_URI) +
         string(GEMINI_URI_PARAMS) + config.apiKey;
}

void Gemini::reconnect() {
  DBG("Reconnecting");
  geminiActive = false;
  if (ws) {
    ws->stop();
  }

  ws = make_shared<WebSocket>();
  ws->onOpen = [this]() {
    DBG("GEMINI ++++++ Socket opened!");
    if (onConnected) onConnected();
    sendSetup();
  };
  ws->onMessage = [this](json payload) {
    geminiActive = true;
    if (onConnected) onConnected();
    parseMessage(payload);
  };
  ws->onClose = [this](json payload) {
    DBG("GEMINI ++++++ Socket closed!");
    geminiActive = false;
    if (onClose) onClose(payload);
  };

  connectWebSocket();
}

void Gemini::connectWebSocket() {
  if (config.apiKey.empty()) {
    DBG("No API key set");
    return;
  }
  try {
    thread([this]() {
      string uri = websocketUrl();
      ws->start(uri);
    }).detach();
  } catch (websocketpp::exception const& e) {
    DBG(e.what());
  } catch (std::exception const& e) {
    DBG(e.what());
  } catch (...) {
    DBG("other exception");
  }
}

void Gemini::sendMessage(json payload) {
  if (geminiActive) {
    thread([this, payload]() { ws->send(payload); }).detach();
  }
}

void Gemini::sendSetup() {
  GeminiSetupParams params;
  params.setup.model = config.audioModel;
  json payload = params;
  thread([this, payload]() { ws->send(payload); }).detach();
}

void Gemini::initializeGemini() {
  geminiActive = true;
  if (transport != "") {
    sendMessage(transport);
  }
  if (lyriaConfig != "") {
    sendMessage(lyriaConfig);
  }
  if (prompts != "") {
    sendMessage(prompts);
  }
}

void Gemini::resetContext() {
  if (!ws->connected) {
    reconnect();
    return;
  }
  DBG("Sending reset");
  GeminiPlaybackParams params;
  params.playback_control = LiveMusicPlaybackControl::RESET_CONTEXT;
  json reset = params;
  sendMessage(reset);
  if (onContextClear) onContextClear();
}

void Gemini::sendTransport(bool playing) {
  DBG("Sending transport");
  GeminiPlaybackParams params;
  params.playback_control = playing ? LiveMusicPlaybackControl::PLAY
                                    : LiveMusicPlaybackControl::PAUSE;
  transport = params;
  sendMessage(transport);
}

void Gemini::sendConfig(LyriaConfig& lyriaParams) {
  DBG("Sending config");
  GeminiConfigParams params = convertConfig(lyriaParams);
  lyriaConfig = params;
  sendMessage(lyriaConfig);
}

void Gemini::sendPrompts(vector<Prompt> newPrompts) {
  DBG("Sending prompts");
  if (newPrompts.empty()) {
    return;
  }
  GeminiPromptParams params = convertPrompts(newPrompts);
  prompts = params;
  sendMessage(prompts);
}

void Gemini::parseMessage(json payload) {
  if (payload.contains("serverContent")) {
    vector<AudioChunk> audioChunks = payload["serverContent"]["audioChunks"];
    if (onAudioChunks) onAudioChunks(std::move(audioChunks));
  } else if (payload.contains("setupComplete")) {
    DBG("Setup Complete");
    initializeGemini();
  } else if (payload.contains("filteredPrompt")) {
    DBG("Filtered Prompt");
    json filteredPrompt = payload["filteredPrompt"];
    if (onFilteredPrompt) onFilteredPrompt(std::move(filteredPrompt));
  } else {
    DBG("Unknown message: " << payload.dump());
  }
}

GeminiPromptParams Gemini::convertPrompts(vector<Prompt> promptList) {
  GeminiClientContent content;
  vector<WeightedPrompt> weightedPrompts;
  for (auto& prompt : promptList) {
    if (prompt.weight > 0) {
      WeightedPrompt weightedPrompt;
      weightedPrompt.text = prompt.text;
      weightedPrompt.weight = prompt.weight;
      weightedPrompts.push_back(weightedPrompt);
    }
  }
  content.weighted_prompts = weightedPrompts;

  GeminiPromptParams params;
  params.client_content = content;
  return params;
}

GeminiConfigParams Gemini::convertConfig(LyriaConfig& lyriaParams) {
  LiveMusicGenerationConfig c;
  c.temperature = lyriaParams.temperature;
  c.topK = lyriaParams.topk;
  c.guidance = lyriaParams.guidance;
  if (lyriaParams.useDensity) {
    c.density = lyriaParams.density;
  }
  if (lyriaParams.useBrightness) {
    c.brightness = lyriaParams.brightness;
  }
  if (lyriaParams.useBpm) {
    c.bpm = lyriaParams.bpm;
  }
  if (lyriaParams.useScale) {
    c.scale = static_cast<Scale>(lyriaParams.rootNote);
  }
  c.muteBass = lyriaParams.muteBass;
  c.muteDrums = lyriaParams.muteDrums;
  c.onlyBassAndDrums = lyriaParams.muteOther;

  if (lyriaParams.generationQuality == "quality") {
    c.musicGenerationMode = MusicGenerationMode::QUALITY;
  } else if (lyriaParams.generationQuality == "diversity") {
    c.musicGenerationMode = MusicGenerationMode::DIVERSITY;
  } else {
    c.musicGenerationMode = MusicGenerationMode::MUSIC_GENERATION_MODE_UNSPECIFIED;
  }

  GeminiConfigParams params;
  params.music_generation_config = c;
  return params;
}

Gemini::~Gemini() {}

}  // namespace magenta
