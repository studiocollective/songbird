#pragma once

#include "MagentaAliases.h"

namespace magenta {

class WebSocket {
 public:
  explicit WebSocket();
  ~WebSocket();

  function<void()> onOpen;
  function<void(json)> onMessage;
  function<void(json)> onClose;

  void start(string uri);
  void send(json payload);
  void stop();

  atomic<bool> connected{false};

 private:
  void on_socket_init(ws_handler);
  ws_context on_tls_init(ws_handler);
  void on_open(ws_handler);
  void on_message(ws_handler, ws_message);
  void on_close(ws_handler);
  void on_fail(ws_handler);

  ws_client client;
  ws_handler hdl;
  string uri;
  unique_ptr<thread> m_io_thread;
  atomic<bool> m_is_starting{false};
  atomic<bool> m_is_stopping{false};

  // Timing members
  ws_time m_start;
  ws_time m_socket_init;
  ws_time m_tls_init;
  ws_time m_open;
  ws_time m_message;
  ws_time m_close;
};

}  // namespace magenta
