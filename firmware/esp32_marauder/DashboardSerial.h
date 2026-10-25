#pragma once
#include <Arduino.h>
#include "DashboardCore.h"

// Capture Arduino's actual Serial before replacing its project-facing alias.
inline auto& dashboardNativeSerial() { return Serial; }

class DashboardSerial : public Stream {
  DashboardLog<8192> log_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
public:
  using Print::write;
  void begin(unsigned long baud) {
    dashboardNativeSerial().begin(baud);
    dashboardNativeSerial().setTxTimeoutMs(0);
  }
  operator bool() { return bool(dashboardNativeSerial()); }
  int available() override { return dashboardNativeSerial().available(); }
  int read() override { return dashboardNativeSerial().read(); }
  int peek() override { return dashboardNativeSerial().peek(); }
  void flush() override { dashboardNativeSerial().flush(); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t length) override;
  size_t readLog(uint32_t& cursor, char* output, size_t capacity, bool& lost);
  uint32_t logCursor() { portENTER_CRITICAL(&mux_); uint32_t cursor=log_.cursor(); portEXIT_CRITICAL(&mux_); return cursor; }
};

DashboardSerial& dashboardSerial();
#undef Serial
#define Serial dashboardSerial()
