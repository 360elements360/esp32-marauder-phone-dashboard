#include "configs.h"
#ifdef MARAUDER_DASHBOARD
#include "CommandLine.h"
#include "DashboardBridge.h"
#include <SPIFFS.h>
#include <esp_wifi.h>
extern CommandLine cli_obj;
namespace {
  uint8_t workflowPreviousChannel = 1;
  int8_t workflowPreviousHop = -1;
  bool workflowOverride = false;
  bool workflowPreviousPassiveBle = false;
  bool commandSucceeded = true;
}
uint8_t dashboardMode() { return wifi_scan_obj.currentScanMode; }
uint8_t dashboardChannel() { return wifi_scan_obj.set_channel; }
void dashboardDispatch(const String& command) { commandSucceeded = true; cli_obj.runCommand(command); }
bool dashboardCommandSucceeded() { return commandSucceeded; }
void dashboardCommandFailed() { commandSucceeded = false; }
void dashboardStop() {
  wifi_scan_obj.StartScan(WIFI_SCAN_OFF);
  recon_obj.stop();
  // Radio callbacks have stopped; flush the last partial buffer as well.
  buffer_obj.save();
}
bool dashboardTargetsIdle() {
  bool promiscuous = false;
  const esp_err_t result = esp_wifi_get_promiscuous(&promiscuous);
  // A stopped driver cannot invoke a sniffer. Treat other query failures as busy.
  if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_INIT) return false;
  return workflowRadioIdle(dashboardMode(), recon_obj.active(), promiscuous,
    wifi_scan_obj.ble_initialized, wifi_scan_obj.ble_scanning, wifi_scan_obj.bt_cb_busy);
}
size_t dashboardTargetCount(bool bluetooth) {
  if (!dashboardTargetsIdle()) return 0;
  return bluetooth ? ble_devices->size() : access_points->size();
}
bool dashboardTargetAt(bool bluetooth, size_t index, WorkflowTarget& target) {
  if (!dashboardTargetsIdle() || index >= dashboardTargetCount(bluetooth)) return false;
  target = WorkflowTarget();
  if (bluetooth) {
    const BleDevice device = ble_devices->get(index);
    workflowFormatMac(device.mac, target.id);
    workflowCopy(target.name, sizeof(target.name), device.name.c_str());
    target.rssi = device.rssi; target.lastSeen = device.last_seen_ms;
    workflowCopy(target.security, sizeof(target.security), "Not applicable");
  } else {
    const AccessPoint device = access_points->get(index);
    workflowFormatMac(device.bssid, target.id);
    workflowCopy(target.name, sizeof(target.name), device.essid.c_str());
    target.rssi = device.rssi; target.lastSeen = device.last_seen_ms;
    target.channel = device.channel; target.packets = device.packets;
    String security = wifi_scan_obj.security_int_to_string(device.sec);
    security.replace("[", ""); security.replace("]", ""); security.replace("_", " ");
    workflowCopy(target.security, sizeof(target.security), security.c_str());
  }
  return true;
}
bool dashboardFindTarget(bool bluetooth, const char* mac, WorkflowTarget& target) {
  if (!dashboardTargetsIdle()) return false;
  uint8_t identity[6];
  if (!workflowParseMac(mac, identity)) return false;
  for (size_t index = 0, count = dashboardTargetCount(bluetooth); index < count; ++index)
    if (dashboardTargetAt(bluetooth, index, target) && workflowTargetMatches(target, identity)) return true;
  return false;
}
uint8_t dashboardWorkflowMode(const char* kind) {
  // sniffbeacon prints frames; only the passive AP/STA scanner builds the MAC list.
  static const WorkflowModes modes = {WIFI_SCAN_OFF, WIFI_SCAN_AP_STA, BT_SCAN_ALL,
    WIFI_SCAN_SIG_STREN, BT_SCAN_FOX_HUNT, WIFI_SCAN_RAW_CAPTURE};
  return workflowRadioMode(kind, modes);
}
bool dashboardStartWorkflow(const char* kind, uint8_t channel, const WorkflowTarget* target) {
  if (!dashboardTargetsIdle() || workflowInspect(kind)) return false;
  #ifndef HAS_BT
    if (workflowBluetooth(kind)) return false;
  #endif
  // Foxhunt appends samples but does not open a file. Never append them to the
  // previous capture's buffer when starting a guided signal task.
  buffer_obj.stopRecording();
  workflowPreviousChannel = wifi_scan_obj.set_channel;
  workflowPreviousHop = wifi_scan_obj.dashboardChannelHop;
  workflowPreviousPassiveBle = wifi_scan_obj.dashboardPassiveBle;
  workflowOverride = true;
  wifi_scan_obj.dashboardPassiveBle = true;
  if (!workflowBluetooth(kind)) {
    wifi_scan_obj.dashboardChannelHop = channel ? 0 : 1;
    wifi_scan_obj.set_channel = channel ? channel : 1;
  }
  if (workflowSignal(kind)) {
    uint8_t mac[6];
    if (!target || !workflowParseMac(target->id, mac)) return false;
    wifi_scan_obj.setFoxHuntTarget(mac, target->name, target->rssi, channel,
      workflowBluetooth(kind), workflowBluetooth(kind) ? String(target->id) : String(""));
  }
  wifi_scan_obj.StartScan(dashboardWorkflowMode(kind), TFT_GREEN);
  return dashboardMode() == dashboardWorkflowMode(kind);
}
void dashboardWorkflowRestore() {
  if (!workflowOverride) return;
  wifi_scan_obj.dashboardChannelHop = workflowPreviousHop;
  wifi_scan_obj.set_channel = workflowPreviousChannel;
  wifi_scan_obj.dashboardPassiveBle = workflowPreviousPassiveBle;
  wifi_scan_obj.fox_hunt_target.active = false;
  workflowOverride = false;
}
void dashboardToolRadioPrepare(uint8_t channel, bool applyChannel) {
  buffer_obj.stopRecording();
  workflowPreviousChannel = wifi_scan_obj.set_channel;
  workflowPreviousHop = wifi_scan_obj.dashboardChannelHop;
  workflowPreviousPassiveBle = wifi_scan_obj.dashboardPassiveBle;
  workflowOverride = true;
  wifi_scan_obj.dashboardPassiveBle = true;
  if (applyChannel) {
    wifi_scan_obj.dashboardChannelHop = channel ? 0 : 1;
    wifi_scan_obj.set_channel = channel ? channel : 1;
  }
}
bool dashboardCaptureReady() {
  return settings_obj.loadSetting<bool>("SavePCAP") && SPIFFS.totalBytes() &&
    SPIFFS.totalBytes() - SPIFFS.usedBytes() >= 65536;
}
bool dashboardCaptureHealthy() { return buffer_obj.recordingHealthy(); }
uint32_t dashboardCapturePackets() { return buffer_obj.recordingPackets(); }
bool dashboardCapturePath(char* path, size_t capacity) {
  if (!dashboardCaptureHealthy()) return false;
  String name = buffer_obj.getFileName();
  if (name.length() >= capacity) return false;
  workflowCopy(path, capacity, name.c_str());
  return true;
}
int dashboardSignalRssi() { return wifi_scan_obj.fox_hunt_target.rssi; }
#endif
