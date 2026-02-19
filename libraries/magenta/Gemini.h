#pragma once

#include "GeminiTypes.h"
#include "LyriaConfig.h"
#include "WebSocket.h"

namespace magenta {

class Gemini {
 public:
  explicit Gemini();
  ~Gemini();

  void initialize(string apiKey);
  string websocketUrl();
  void connectWebSocket();

  void sendMessage(json payload);
  void sendSetup();
  void initializeGemini();
  void resetContext();
  void reconnect();

  void sendTransport(bool playing);
  void sendConfig(LyriaConfig& config);
  void sendPrompts(vector<Prompt> prompts);

  void parseMessage(json payload);

  function<void(vector<AudioChunk>)> onAudioChunks;
  function<void(json filteredPrompt)> onFilteredPrompt;
  function<void()> onContextClear;
  function<void()> onConnected;
  function<void(json response)> onClose;

  GeminiPromptParams convertPrompts(vector<Prompt> prompts);
  GeminiConfigParams convertConfig(LyriaConfig& config);

  bool isConnected() const { return geminiActive.load(); }

 private:
  GeminiConfig config;
  shared_ptr<WebSocket> ws;
  atomic<bool> geminiActive;

  // Save the most recent values so we can resend on reconnection
  json transport;
  json lyriaConfig;
  json prompts;

  mutex url_mutex;
};

}  // namespace magenta
