#include "WebSocket.h"

#include <iostream>
#include <string>

namespace magenta {

WebSocket::WebSocket() {
  client.set_access_channels(websocketpp::log::alevel::none);
  client.set_error_channels(websocketpp::log::elevel::all);

  client.init_asio();

  client.set_socket_init_handler(bind(&WebSocket::on_socket_init, this, _1));
  client.set_tls_init_handler(bind(&WebSocket::on_tls_init, this, _1));
  client.set_message_handler(bind(&WebSocket::on_message, this, _1, _2));
  client.set_open_handler(bind(&WebSocket::on_open, this, _1));
  client.set_close_handler(bind(&WebSocket::on_close, this, _1));
  client.set_fail_handler(bind(&WebSocket::on_fail, this, _1));
}

WebSocket::~WebSocket() {
  stop();
}

void WebSocket::start(string uri) {
  if (m_is_starting.load() || connected.load()) {
    return;
  }

  if (m_is_stopping.load() || (m_io_thread && m_io_thread->joinable())) {
    this->stop();
    return;
  }

  this->uri = uri;
  m_is_starting.store(true);
  m_is_stopping.store(false);

  websocketpp::lib::error_code ec;
  ws_connection con = client.get_connection(this->uri, ec);

  if (ec) {
    DBG("WebSocket connection error: " << ec.message());
    return;
  }

  client.connect(con);

  m_start = std::chrono::high_resolution_clock::now();
  m_io_thread = std::make_unique<std::thread>([this]() {
    try {
      client.run();
    } catch (const std::exception& e) {
      DBG("Exception in WebSocket thread: " << e.what());
    } catch (...) {
      DBG("Unknown exception in WebSocket thread.");
    }
  });
}

void WebSocket::on_socket_init(ws_handler) {
  m_socket_init = std::chrono::high_resolution_clock::now();
}

ws_context WebSocket::on_tls_init(ws_handler) {
  m_tls_init = std::chrono::high_resolution_clock::now();
  ws_context ctx = websocketpp::lib::make_shared<asio::ssl::context>(
      asio::ssl::context::tlsv12);

  try {
    ctx->set_options(
        asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 | asio::ssl::context::single_dh_use);
  } catch (std::exception& e) {
    DBG("Exception in on_tls_init: " << e.what());
  }
  return ctx;
}

void WebSocket::on_open(ws_handler hdl_param) {
  m_open = std::chrono::high_resolution_clock::now();
  this->hdl = hdl_param;
  DBG("WebSocket opened");
  connected.store(true);
  m_is_starting.store(false);
  if (onOpen) {
    onOpen();
  }
}

void WebSocket::send(json payload) {
  if (connected.load()) {
    client.send(hdl, payload.dump(), websocketpp::frame::opcode::text);
  }
}

void WebSocket::on_message(ws_handler, ws_message m) {
  m_message = std::chrono::high_resolution_clock::now();
  if (onMessage) {
    onMessage(json::parse(m->get_payload()));
  }
}

void WebSocket::on_close(ws_handler hdl_param) {
  connected.store(false);

  ws_connection con = client.get_con_from_hdl(hdl_param);
  if (!con) return;

  try {
    json payload;
    payload["code"] = con->get_remote_close_code();
    payload["reason"] = con->get_remote_close_reason();
    if (onClose) {
      onClose(payload);
    }
  } catch (const std::exception& e) {
    DBG("Unexpected socket close error");
  }

  m_close = std::chrono::high_resolution_clock::now();
  DBG("WebSocket closed");
}

void WebSocket::on_fail(ws_handler hdl_param) {
  m_is_starting.store(false);
  connected.store(false);

  ws_connection con = client.get_con_from_hdl(hdl_param);
  if (!con) return;

  try {
    json payload;
    payload["code"] = con->get_remote_close_code();
    payload["reason"] = con->get_remote_close_reason();
    if (onClose) {
      onClose(payload);
    }
  } catch (const std::exception& e) {
    DBG("Unexpected socket fail error");
  }

  DBG("WebSocket failure: " << con->get_ec().value() << " - "
                             << con->get_ec().message());
}

void WebSocket::stop() {
  if (m_is_stopping.load()) {
    if (m_io_thread && m_io_thread->joinable()) {
      m_io_thread->join();
    }
    return;
  }

  m_is_stopping.store(true);

  if (connected.load()) {
    websocketpp::lib::error_code ec_close;
    if (hdl.lock()) {
      client.close(hdl, websocketpp::close::status::going_away,
                   "Shutting down", ec_close);
    }
  }

  if (!client.stopped()) {
    client.stop();
  }

  if (m_io_thread && m_io_thread->joinable()) {
    m_io_thread->join();
  }
  m_io_thread.reset();
  connected.store(false);
  m_is_stopping.store(false);
  m_is_starting.store(false);
}

}  // namespace magenta
