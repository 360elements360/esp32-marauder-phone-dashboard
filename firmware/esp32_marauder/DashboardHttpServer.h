#pragma once
#include <WebServer.h>
#include <lwip/sockets.h>
#include "DashboardCore.h"

// Wait nonblockingly for a bounded request, then use Arduino's routing/response API.
// No upload/chunked/form parser or request-sized allocation is reached first.
class DashboardHttpServer : public WebServer {
  char headers_[2049] = {};
  void reject(int code) {
    _currentClient.printf("HTTP/1.1 %d Rejected\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", code);
    _currentClient.stop();
    _currentClient = NetworkClient();
    _currentStatus = HC_NONE;
  }
public:
  DashboardHttpServer() : WebServer(80) {}
  void handleClient() {
    if (_currentStatus == HC_NONE) {
      _currentClient = _server.accept();
      if (!_currentClient) return;
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
    }
    if (_currentStatus == HC_WAIT_READ) {
      if (!_currentClient.connected()) { reject(400); return; }
      if (uint32_t(millis() - _statusChange) > 1500) { reject(408); return; }
      int available = _currentClient.available();
      if (available <= 0) return;
      // No NetworkClient reads have occurred: inspect the socket without consuming it.
      int peeked = recv(_currentClient.fd(), headers_, 2048, MSG_PEEK | MSG_DONTWAIT);
      if (peeked <= 0) return;
      size_t length = peeked;
      headers_[length] = 0;
      size_t total = 0;
      int result = dashboardInspectHttp(headers_, total);
      if (!result) return;
      if (result != 200) { reject(result); return; }
      if (size_t(available) < total) return;
      if (size_t(available) > total) { reject(413); return; }
    }
    WebServer::handleClient();
  }
};
