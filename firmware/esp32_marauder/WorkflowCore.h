#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// No Arduino objects or list indexes cross the HTTP/radio boundary.
struct WorkflowTarget {
  char id[49] = {}, name[97] = {}, security[32] = {};
  int rssi = -128;
  uint8_t channel = 0;
  uint32_t packets = 0, lastSeen = 0;
};

inline void workflowCopy(char* to, size_t size, const char* from) {
  if (size) { strncpy(to, from ? from : "", size - 1); to[size - 1] = 0; }
}
inline bool workflowParseMac(const char* text, uint8_t mac[6]) {
  if (!text || strlen(text) != 17) return false;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned value = 0;
    for (unsigned n = 0; n < 2; ++n) {
      char c = text[i * 3 + n];
      if (c >= 'A' && c <= 'F') c += 'a' - 'A';
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
      value = value * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
    }
    mac[i] = uint8_t(value);
    if (i < 5 && text[i * 3 + 2] != ':') return false;
  }
  return true;
}
inline void workflowFormatMac(const uint8_t mac[6], char text[18]) {
  snprintf(text, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
inline bool workflowTargetMatches(const WorkflowTarget& target, const uint8_t mac[6]) {
  uint8_t candidate[6];
  return workflowParseMac(target.id, candidate) && !memcmp(candidate, mac, 6);
}
inline bool workflowBluetooth(const char* kind) { return !strncmp(kind, "ble_", 4); }
inline bool workflowDiscover(const char* kind) { return !strcmp(kind, "wifi_discover") || !strcmp(kind, "ble_discover"); }
inline bool workflowInspect(const char* kind) { return !strcmp(kind, "wifi_inspect") || !strcmp(kind, "ble_inspect"); }
inline bool workflowSignal(const char* kind) { return !strcmp(kind, "wifi_signal") || !strcmp(kind, "ble_signal"); }
struct WorkflowModes {
  uint8_t idle, wifiDiscovery, bleDiscovery, wifiSignal, bleSignal, wifiCapture;
};
inline uint8_t workflowRadioMode(const char* kind, const WorkflowModes& modes) {
  if (!strcmp(kind, "wifi_discover")) return modes.wifiDiscovery;
  if (!strcmp(kind, "ble_discover")) return modes.bleDiscovery;
  if (!strcmp(kind, "wifi_signal")) return modes.wifiSignal;
  if (!strcmp(kind, "ble_signal")) return modes.bleSignal;
  if (!strcmp(kind, "wifi_capture")) return modes.wifiCapture;
  return modes.idle;
}
inline bool workflowChannelHopAllowed(int8_t temporary, bool saved, bool preferenceControlled) {
  if (temporary >= 0) return temporary != 0;
  return !preferenceControlled || saved;
}
// Beacon length excludes FCS. Locate and validate the complete SSID IE before
// consumers read its bytes; SSIDs are at most 32 octets and need not be first.
inline bool workflowBeaconSsid(const uint8_t* frame, size_t length,
    const uint8_t*& name, size_t& nameLength) {
  name = nullptr; nameLength = 0;
  if (!frame || length < 38) return false;
  for (size_t offset = 36; offset + 2 <= length;) {
    const uint8_t tag = frame[offset], count = frame[offset + 1];
    offset += 2;
    if (count > length - offset) return false;
    if (tag == 0) {
      if (count > 32) return false;
      name = frame + offset; nameLength = count; return true;
    }
    offset += count;
  }
  return false;
}
inline int workflowValidate(const char* kind, int seconds, int channel, const char* target) {
  if (!kind || !(workflowDiscover(kind) || workflowInspect(kind) || workflowSignal(kind) || !strcmp(kind, "wifi_capture"))) return 400;
  if (seconds < 1 || seconds > 300 || channel < 0 || channel > 14) return 400;
  uint8_t mac[6];
  const bool hasTarget = target && *target;
  if (hasTarget && !workflowParseMac(target, mac)) return 400;
  if ((workflowInspect(kind) || workflowSignal(kind)) && !hasTarget) return 400;
  if (!strcmp(kind, "wifi_capture") && !channel && !hasTarget) return 400;
  return 0;
}
inline bool workflowManualAllowed(bool active, const char* command) {
  return !active || !strcmp(command, "stopscan") || !strcmp(command, "dashboard recover");
}
// An initialized Wi-Fi driver may belong only to the dashboard control AP.
// Target safety depends on actual callback activity, not driver initialization.
inline bool workflowRadioIdle(uint8_t mode, bool recon, bool wifiCallbacks,
    bool bleInitialized, bool bleScanning, bool bleCallback) {
  return mode == 0 && !recon && !wifiCallbacks && !bleInitialized && !bleScanning && !bleCallback;
}
enum class WorkflowState : uint8_t { Idle, Queued, Running, Recovering, Completed, Cancelled, Failed };
class WorkflowCore {
  uint32_t started_ = 0, elapsedMs_ = 0;
public:
  uint32_t id = 0, count = 0, logStart = 0;
  uint16_t seconds = 0;
  uint8_t channel = 0;
  int rssi = -128;
  char kind[16] = {}, tool[49] = {}, target[49] = {}, targetName[97] = {}, file[129] = {}, error[161] = {};
  WorkflowState state = WorkflowState::Idle;
  bool active() const { return state == WorkflowState::Queued || state == WorkflowState::Running || state == WorkflowState::Recovering; }
  const char* stateName() const {
    const char* names[] = {"idle", "queued", "running", "recovering", "completed", "cancelled", "failed"};
    return names[static_cast<unsigned>(state)];
  }
  bool queue(const char* type, uint16_t duration, uint8_t selectedChannel, const WorkflowTarget* selected, uint32_t now) {
    if (active() || workflowValidate(type, duration, selectedChannel, selected ? selected->id : "")) return false;
    uint32_t next = id + 1; if (!next) next = 1;
    *this = WorkflowCore(); id = next;
    workflowCopy(kind, sizeof(kind), type); seconds = duration; channel = selectedChannel;
    if (selected) {
      workflowCopy(target, sizeof(target), selected->id); workflowCopy(targetName, sizeof(targetName), selected->name);
      rssi = selected->rssi; count = selected->packets;
    }
    started_ = now; state = WorkflowState::Queued; return true;
  }
  bool queueTool(const char* name, uint16_t duration, uint8_t selectedChannel, uint32_t now) {
    if (active() || !name || !*name || strlen(name) >= sizeof(tool) || duration < 1 || duration > 300 || selectedChannel > 14) return false;
    uint32_t next = id + 1; if (!next) next = 1;
    *this = WorkflowCore(); id = next;
    workflowCopy(kind, sizeof(kind), "tool"); workflowCopy(tool, sizeof(tool), name);
    seconds = duration; channel = selectedChannel; started_ = now; state = WorkflowState::Queued; return true;
  }
  void running(uint32_t now) { started_ = now; state = WorkflowState::Running; }
  uint32_t elapsedMs(uint32_t now) const { return state == WorkflowState::Running ? uint32_t(now - started_) : elapsedMs_; }
  uint32_t elapsed(uint32_t now) const { uint32_t n = elapsedMs(now) / 1000; return n > seconds ? seconds : n; }
  uint32_t remaining(uint32_t now) const {
    if (!active() || state == WorkflowState::Recovering) return 0;
    uint32_t ms = elapsedMs(now), duration = uint32_t(seconds) * 1000;
    return ms >= duration ? 0 : (duration - ms + 999) / 1000;
  }
  bool expired(uint32_t now) const { return state == WorkflowState::Running && uint32_t(now - started_) >= uint32_t(seconds) * 1000; }
  void recover(uint32_t now) { elapsedMs_ = elapsedMs(now); state = WorkflowState::Recovering; }
  bool cancel(uint32_t requested, uint32_t now) { if (!active() || requested != id) return false; recover(now); return true; }
  void finish(WorkflowState result, const char* why = "") { state = result; workflowCopy(error, sizeof(error), why); }
};
