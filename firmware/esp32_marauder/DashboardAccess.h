#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Kept independent of Wi-Fi/NVS so startup and rotation rules are testable.
class DashboardAccess {
  char password_[64] = "marauder-setup", pending_[64] = {};
  uint32_t at_ = 0;
  bool changing_ = false;
public:
  static bool valid(const char* value, size_t length) {
    if (!value || length < 8 || length > 63) return false;
    if (value[0] == ' ' || value[length - 1] == ' ') return false;
    for (size_t i = 0; i < length; ++i)
      if (static_cast<unsigned char>(value[i]) < 32 || static_cast<unsigned char>(value[i]) > 126) return false;
    return true;
  }
  void load(const char* saved, size_t length) {
    if (!valid(saved, length)) { saved = "marauder-setup"; length = 14; }
    memcpy(password_, saved, length); password_[length] = 0;
    memset(pending_, 0, sizeof(pending_)); changing_ = false;
  }
  const char* password() const { return password_; }
  bool setupRequired() const { return !strcmp(password_, "marauder-setup"); }
  bool changing() const { return changing_; }
  bool canChange(const char* value, size_t length) const {
    return !changing_ && valid(value, length) &&
      !(length == 14 && !memcmp(value, "marauder-setup", 14)) &&
      !(length == strlen(password_) && !memcmp(value, password_, length));
  }
  // Call only after storage confirms the new password was committed.
  bool stage(const char* value, size_t length, uint32_t now) {
    if (!canChange(value, length)) return false;
    memcpy(pending_, value, length); pending_[length] = 0;
    at_ = now; changing_ = true; return true;
  }
  bool apply(uint32_t now) {
    if (!changing_ || uint32_t(now - at_) < 1500) return false;
    memcpy(password_, pending_, sizeof(password_));
    memset(pending_, 0, sizeof(pending_)); changing_ = false; return true;
  }
};
