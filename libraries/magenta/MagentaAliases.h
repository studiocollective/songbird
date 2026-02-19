#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <string>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <map>

#define ASIO_STANDALONE
#define _WEBSOCKETPP_CPP11_STRICT_
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

#include <nlohmann/json.hpp>

namespace magenta {

using json = nlohmann::json;

using std::array;
using std::atomic;
using std::exception;
using std::function;
using std::lock_guard;
using std::make_shared;
using std::make_unique;
using std::map;
using std::mutex;
using std::nullopt;
using std::optional;
using std::pair;
using std::shared_ptr;
using std::string;
using std::thread;
using std::to_string;
using std::unique_ptr;
using std::vector;

using juce::Array;
using juce::AudioBuffer;
using juce::Base64;
using juce::Decibels;
using juce::File;
using juce::MemoryBlock;
using juce::MemoryOutputStream;
using juce::MidiBuffer;
using juce::String;
using juce::dsp::AudioBlock;
using juce::dsp::BallisticsFilter;
using juce::dsp::ProcessContextNonReplacing;
using juce::dsp::ProcessSpec;

using Var = juce::var;

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using ws_dur = std::chrono::duration<int, std::micro>;
using ws_time = std::chrono::high_resolution_clock::time_point;
using ws_client = websocketpp::client<websocketpp::config::asio_tls_client>;
using ws_handler = websocketpp::connection_hdl;
using ws_connection = ws_client::connection_ptr;
using ws_message = ws_client::message_ptr;
using ws_context = websocketpp::lib::shared_ptr<asio::ssl::context>;

}  // namespace magenta
