#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// USB being plugged in does not mean a host program is consuming its output.
// Mirror only immediately writable bytes; the dashboard keeps its own full tail.
template <typename SerialType>
size_t dashboardForwardSerial(SerialType& output, const uint8_t* bytes, size_t length) {
  if (!output) return 0;
  int capacity = output.availableForWrite();
  if (capacity <= 0) return 0;
  size_t count = length < static_cast<size_t>(capacity) ? length : static_cast<size_t>(capacity);
  return output.write(bytes, count);
}

// Inspect the headers BEFORE Arduino's parser can allocate a request body.
// 0 = incomplete; 200 = bounded request; other values are HTTP rejection codes.
inline int dashboardInspectHttp(const char* input, size_t& requestLength) {
  const char* terminator = strstr(input, "\r\n\r\n");
  if (!terminator) return strlen(input) >= 2048 ? 431 : 0;
  if (size_t(terminator - input) + 4 > 2048) return 431;
  const char* lineEnd = strstr(input, "\r\n");
  if (!lineEnd || lineEnd - input > 256) return 414;
  const bool post = !strncmp(input, "POST ", 5);
  if (!post && strncmp(input, "GET ", 4)) return 405;
  if (post && strncmp(input, "POST /api/command ", 18) && strncmp(input, "POST /api/file/delete ", 22) && strncmp(input, "POST /api/workflow ", 19) && strncmp(input,"POST /api/template ",19) && strncmp(input,"POST /api/access ",17)) return 404;
  bool lengthSeen = false, typeSeen = false, json = false;
  size_t bodyLength = 0;
  auto equal = [](const char* start, size_t length, const char* expected) {
    if (strlen(expected) != length) return false;
    for (size_t i = 0; i < length; ++i) {
      char c = start[i]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
      if (c != expected[i]) return false;
    }
    return true;
  };
  for (const char* line = lineEnd + 2; line < terminator; line = lineEnd + 2) {
    lineEnd = strstr(line, "\r\n");
    if (!lineEnd || lineEnd - line > 256 || *line == ' ' || *line == '\t') return 431;
    const char* colon = static_cast<const char*>(memchr(line, ':', lineEnd - line));
    if (!colon) return 400;
    const char* value = colon + 1;
    while (value < lineEnd && (*value == ' ' || *value == '\t')) ++value;
    const char* valueEnd = lineEnd;
    while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) --valueEnd;
    if (equal(line, colon - line, "transfer-encoding")) return 400;
    if (equal(line, colon - line, "content-length")) {
      if (lengthSeen || value == valueEnd) return 400;
      lengthSeen = true;
      for (const char* p = value; p < valueEnd; ++p) {
        if (*p < '0' || *p > '9') return 400;
        bodyLength = bodyLength * 10 + (*p - '0');
        if (bodyLength > 512) return 413;
      }
    }
    if (equal(line, colon - line, "content-type")) {
      if (typeSeen) return 400;
      typeSeen = true;
      const char* semi = static_cast<const char*>(memchr(value, ';', valueEnd - value));
      json = equal(value, (semi ? semi : valueEnd) - value, "application/json");
    }
  }
  if (post && !json) return 415;
  if (typeSeen && !json) return 415;
  if (post && !lengthSeen) return 411;
  if (!post && bodyLength) return 400;
  requestLength = (terminator - input) + 4 + bodyLength;
  return 200;
}

class DashboardBody {
  char bytes_[513] = {};
  size_t size_ = 0;
  bool valid_ = true;
public:
  void reset() { size_ = 0; valid_ = true; bytes_[0] = 0; }
  bool append(const uint8_t* bytes, size_t length) {
    if (!valid_ || length > 512 - size_) { valid_ = false; return false; }
    memcpy(bytes_ + size_, bytes, length); size_ += length; bytes_[size_] = 0;
    return true;
  }
  const char* data() const { return bytes_; }
  size_t size() const { return size_; }
  bool valid() const { return valid_; }
};

// Caller serializes access: callbacks write; the main loop reads.
template <size_t Capacity> class DashboardLog {
  char bytes_[Capacity] = {};
  uint32_t end_ = 0;
  size_t size_ = 0;
public:
  uint32_t cursor() const { return end_; }
  void append(const char* bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
      bytes_[end_++ % Capacity] = bytes[i];
      if (size_ < Capacity) ++size_;
    }
  }
  size_t read(uint32_t& cursor, char* output, size_t capacity, bool& lost) const {
    const uint32_t available = end_ - cursor;
    lost = available > size_;
    if (lost) cursor = end_ - size_;
    size_t count = end_ - cursor;
    if (count > capacity) count = capacity;
    for (size_t i = 0; i < count; ++i) output[i] = bytes_[(cursor + i) % Capacity];
    cursor += count;
    output[count] = 0; // output must provide capacity + 1 bytes
    return count;
  }
};

struct DashboardCommand { char text[257] = {}; uint16_t seconds = 30; };
class DashboardQueue {
  DashboardCommand entries_[4];
  uint8_t first_ = 0, count_ = 0;
public:
  static bool valid(const char* text) {
    if (!text || !*text) return false;
    size_t n = 0;
    for (; text[n] && n <= 256; ++n)
      if (static_cast<unsigned char>(text[n]) < 32 || text[n] == 127) return false;
    return n > 0 && n <= 256;
  }
  bool push(const char* text, uint16_t seconds) {
    if (count_ == 4 || seconds > 3600 || !valid(text)) return false;
    auto& command = entries_[(first_ + count_) % 4];
    strcpy(command.text, text);
    command.seconds = seconds;
    ++count_;
    return true;
  }
  bool pop(DashboardCommand& result) {
    if (!count_) return false;
    result = entries_[first_];
    first_ = (first_ + 1) % 4;
    --count_;
    return true;
  }
  uint8_t size() const { return count_; }
};

class DashboardTimer {
  uint32_t since_ = 0, duration_ = 0;
public:
  void start(uint32_t now, uint16_t seconds) { since_ = now; duration_ = uint32_t(seconds) * 1000; }
  bool expired(uint32_t now) const { return duration_ && uint32_t(now - since_) >= duration_; }
  uint32_t remaining(uint32_t now) const {
    if (!duration_ || expired(now)) return 0;
    return (duration_ - uint32_t(now - since_) + 999) / 1000;
  }
  void clear() { duration_ = 0; }
  void afterCommand(uint32_t now, uint16_t seconds, uint32_t before, uint32_t after, bool running) {
    if (before == after) return;
    if (running) start(now, seconds);
    else clear();
  }
};

inline bool dashboardSafePath(const char* path) {
  if (!path || path[0] != '/' || strlen(path) > 128 || strstr(path, "..") ||
      strchr(path, '\\') || strchr(path, '\r') || strchr(path, '\n')) return false;
  // SPIFFS settings contain credentials; only public capture/log formats may be downloaded.
  const char* ext = strrchr(path, '.');
  return ext && (!strcmp(ext, ".pcap") || !strcmp(ext, ".csv") ||
                 !strcmp(ext, ".log") || !strcmp(ext, ".gpx") || !strcmp(ext, ".txt"));
}
