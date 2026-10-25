#include "configs.h"
#ifdef MARAUDER_DASHBOARD
#include "DashboardSerial.h"

DashboardSerial& dashboardSerial() {
  static DashboardSerial instance;
  return instance;
}
size_t DashboardSerial::write(const uint8_t* bytes, size_t length) {
  // Keep UART/USB behavior while avoiding disconnected USB backpressure.
  dashboardForwardSerial(dashboardNativeSerial(), bytes, length);
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < length; ++i) {
    uint8_t c = bytes[i];
    if (c >= 32 || c == '\n' || c == '\r' || c == '\t') {
      char ch = static_cast<char>(c);
      log_.append(&ch, 1);
    }
  }
  portEXIT_CRITICAL(&mux_);
  return length;
}
size_t DashboardSerial::readLog(uint32_t& cursor, char* output, size_t capacity, bool& lost) {
  portENTER_CRITICAL(&mux_);
  size_t count = log_.read(cursor, output, capacity, lost);
  portEXIT_CRITICAL(&mux_);
  return count;
}
#endif
