#include "DeviceDashboard.h"
#ifdef MARAUDER_DASHBOARD
#include "DashboardHttpServer.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "DashboardBridge.h"
#include "DashboardSerial.h"
#include "DashboardAssets.h"
#include <Preferences.h>
#include <SPIFFS.h>
#include <esp_wifi.h>
#include <new>

namespace { DashboardHttpServer dashboardHttp; }
DeviceDashboard::DeviceDashboard() : http_(dashboardHttp) {}
DeviceDashboard dashboard_obj;

void DeviceDashboard::reply(int code, const char* message) {
  StaticJsonDocument<192> doc;
  doc[code < 400 ? "message" : "error"] = message;
  String body;
  serializeJson(doc, body);
  http_.sendHeader("Cache-Control", "no-store");
  http_.send(code, "application/json", body);
}

bool DeviceDashboard::authorized(bool mutation) {
  http_.sendHeader("Cache-Control", "no-store");
  if (http_.header("X-Device-Key") != access_.password()) {
    reply(401, "Enter your device password. New installation? Use Set up new device.");
    return false;
  }
  if (mutation && http_.hasHeader("Origin") &&
      http_.header("Origin") != "http://" + http_.hostHeader()) {
    reply(403, "Cross-origin commands are disabled.");
    return false;
  }
  if ((access_.setupRequired() || access_.changing()) && http_.uri() != "/api/status" &&
      !(http_.uri() == "/api/access" && !access_.changing())) {
    reply(409, access_.changing() ? "Wi-Fi password is changing; reconnect with the new password." : "Choose your own Wi-Fi password before using the device.");
    return false;
  }
  return true;
}

void DeviceDashboard::credentials() {
  // Deliberately bypass the mirrored log: credentials must only appear on USB.
  dashboardNativeSerial().printf("\nDashboard Wi-Fi: %s\nPassword: %s\nOpen: http://192.168.4.1\n", ssid_, access_.password());
}

void DeviceDashboard::diagnostics() {
  StaticJsonDocument<1280> doc;
  wifi_mode_t mode = WIFI_MODE_NULL;
  bool promiscuous = false;
  uint8_t channel = 0; wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  wifi_config_t config = {};
  doc["modeResult"] = esp_wifi_get_mode(&mode); doc["mode"] = int(mode);
  doc["channelResult"] = esp_wifi_get_channel(&channel, &secondary); doc["channel"] = channel;
  doc["promiscuousResult"] = esp_wifi_get_promiscuous(&promiscuous); doc["promiscuous"] = promiscuous;
  const esp_err_t result = esp_wifi_get_config(WIFI_IF_AP, &config);
  doc["apConfigResult"] = result;
  if (result == ESP_OK) {
    char name[33] = {};
    const size_t length = config.ap.ssid_len ? (config.ap.ssid_len > 32 ? 32 : config.ap.ssid_len) : strnlen(reinterpret_cast<const char*>(config.ap.ssid), 32);
    memcpy(name, config.ap.ssid, length);
    doc["apSsid"] = name; doc["apChannel"] = config.ap.channel;
    doc["apAuth"] = int(config.ap.authmode); doc["apMaxClients"] = config.ap.max_connection;
    doc["apBeaconInterval"] = config.ap.beacon_interval; doc["apHidden"] = config.ap.ssid_hidden;
  }
  doc["apIP"] = WiFi.softAPIP().toString(); doc["apStarted"] = WiFi.AP.started();
  doc["clients"] = WiFi.softAPgetStationNum(); doc["listening"] = listening_; doc["scanMode"] = dashboardMode();
  doc["job"] = workflow_.id; doc["kind"] = workflow_.kind; doc["state"] = workflow_.stateName();
  doc["elapsed"] = workflow_.elapsed(millis()); doc["remaining"] = workflow_.remaining(millis());
  doc["targetsIdle"] = dashboardTargetsIdle(); doc["queued"] = queue_.size();
  String json; serializeJson(doc, json);
  const String line = "[Dashboard radio] " + json + "\r\n";
  if (toolDispatch_) {
    // Internal read controls need browser-visible output. The normal mirror
    // retains this redacted diagnostic while forwarding USB without blocking.
    Serial.print(line);
    return;
  }
  // A diagnostic exceeds the 256-byte USB TX ring. This explicit USB request
  // gets a short bounded drain opportunity; ordinary logging remains nonblocking.
  auto& usb = dashboardNativeSerial();
  size_t sent = 0;
  const uint32_t since = millis();
  while (sent < line.length() && usb && uint32_t(millis() - since) < 200) {
    sent += dashboardForwardSerial(usb, reinterpret_cast<const uint8_t*>(line.c_str()) + sent, line.length() - sent);
    if (sent < line.length()) delay(1);
  }
}

void DeviceDashboard::begin() {
  boot_ = esp_random();
  snprintf(ssid_, sizeof(ssid_), "Marauder-%06X", unsigned(ESP.getEfuseMac() & 0xffffff));
  Preferences prefs;
  prefs.begin("dashboard", false);
  String saved = prefs.getString("password", "");
  access_.load(saved.c_str(), saved.length());
  prefs.end();
  const char* keys[] = {"X-Device-Key", "Origin"};
  http_.collectHeaders(keys, 2);
  http_.on("/", HTTP_GET, [this]() {
    http_.sendHeader("Content-Encoding", "gzip");
    http_.sendHeader("Cache-Control", "no-cache");
    http_.sendHeader("X-Content-Type-Options", "nosniff");
    http_.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'");
    http_.send_P(200, "text/html; charset=utf-8", (const char*)DASHBOARD_HTML, sizeof(DASHBOARD_HTML));
  });
  http_.on("/api/status", HTTP_GET, [this]() { if (authorized()) status(); });
  http_.on("/api/log", HTTP_GET, [this]() { if (authorized()) logs(); });
  http_.on("/api/access", HTTP_POST, [this]() { if (authorized(true)) changeAccess(); }, [this]() { bodyChunk(); });
  http_.on("/api/command", HTTP_POST, [this]() { if (authorized(true)) command(); }, [this]() { bodyChunk(); });
  http_.on("/api/targets", HTTP_GET, [this]() { if (authorized()) targets(); });
  http_.on("/api/workflow", HTTP_POST, [this]() { if (authorized(true)) workflow(); }, [this]() { bodyChunk(); });
  http_.on("/api/files", HTTP_GET, [this]() { if (authorized()) files(false); });
  http_.on("/api/file", HTTP_GET, [this]() { if (authorized()) files(true); });
  http_.on("/api/file/delete", HTTP_POST, [this]() { if (authorized(true)) deleteFile(); }, [this]() { bodyChunk(); });
  http_.on("/api/template", HTTP_POST, [this]() { if (authorized(true)) templateUpload(); }, [this]() { bodyChunk(); });
  http_.on("/favicon.ico", HTTP_GET, [this]() { http_.send(204); });
  http_.onNotFound([this]() { reply(404, "Not found"); });
  restoreAP();
  credentials();
}

void DeviceDashboard::restoreAP() {
  if (listening_) http_.stop();
  listening_ = false;
  wifi_mode_t mode = WIFI_MODE_NULL;
  esp_wifi_get_mode(&mode);
  // Retain an intentional station connection; recreate AP credentials/configuration.
  if (!WiFi.mode((mode & WIFI_MODE_STA) ? WIFI_AP_STA : WIFI_AP)) {
    retryAt_ = millis() + 3000;
    return;
  }
  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(ssid_, access_.password(), 1, false, 2)) {
    retryAt_ = millis() + 3000;
    return;
  }
  http_.begin();
  listening_ = true;
  retryAt_ = 0;
  Serial.println("[Dashboard] Ready at http://192.168.4.1");
}

void DeviceDashboard::suspend() {
  if (listening_) http_.stop();
  listening_ = false;
}

void DeviceDashboard::recover() {
  timer_.clear();
  if (workflow_.active()) { endWorkflow(WorkflowState::Cancelled); return; }
  dashboardStop();
  restoreAP();
}

void DeviceDashboard::status() {
  StaticJsonDocument<1536> doc;
  doc["version"] = MARAUDER_VERSION;
  doc["board"] = HARDWARE_NAME;
  doc["bootId"] = boot_;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();
  doc["psram"] = ESP.getFreePsram();
  doc["flash"] = ESP.getFlashChipSize();
  doc["mode"] = dashboardMode();
  doc["channel"] = dashboardChannel();
  doc["remaining"] = timer_.remaining(millis());
  doc["queued"] = queue_.size();
  doc["ssid"] = ssid_;
  doc["setupRequired"] = access_.setupRequired();
  doc["accessChanging"] = access_.changing();
  doc["clients"] = WiFi.softAPgetStationNum();
  doc["sd"] = false;
  doc["gps"] = false;
  doc["screen"] = false;
  doc["logCapacity"] = 8192;
  workflowStatus(doc.createNestedObject("workflow"));
  String body; serializeJson(doc, body);
  http_.send(200, "application/json", body);
}

void DeviceDashboard::logs() {
  String value = http_.arg("cursor");
  char* end = nullptr;
  unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (value.isEmpty() || !end || *end || value.length() > 10 || parsed > UINT32_MAX) {
    reply(400, "Invalid log cursor"); return;
  }
  uint32_t cursor = parsed;
  bool lost;
  dashboardSerial().readLog(cursor, logChunk_, 1024, lost);
  StaticJsonDocument<192> doc;
  doc["cursor"] = cursor;
  doc["lost"] = lost;
  doc["text"] = static_cast<const char*>(logChunk_);
  String body; serializeJson(doc, body);
  http_.send(200, "application/json", body);
}

void DeviceDashboard::bodyChunk() {
  auto& chunk = http_.raw();
  if (chunk.status == RAW_START) body_.reset();
  else if (chunk.status == RAW_WRITE) body_.append(chunk.buf, chunk.currentSize);
}

void DeviceDashboard::workflowStatus(JsonObject object) {
  const uint32_t now = millis();
  object["id"] = workflow_.id; object["kind"] = workflow_.kind;
  object["state"] = workflow_.stateName(); object["seconds"] = workflow_.seconds;
  object["elapsed"] = workflow_.elapsed(now); object["remaining"] = workflow_.remaining(now);
  object["target"] = workflow_.target; object["targetName"] = workflow_.targetName;
  object["channel"] = workflow_.channel; object["file"] = workflow_.file;
  object["error"] = workflow_.error; object["count"] = workflow_.count; object["rssi"] = workflow_.rssi;
  object["tool"] = workflow_.tool; object["logStart"] = workflow_.logStart;
  if (workflow_.tool[0] && workflow_.state == WorkflowState::Completed)
    object["output"] = "Command sequence completed. Read the device output for results.";
}

void DeviceDashboard::targets() {
  const String kind = http_.arg("kind"), pageText = http_.arg("page");
  if (!dashboardToolTargetKind(kind.c_str())) { reply(400, "Unknown target kind"); return; }
  uint32_t page = 0;
  if (pageText.length()) {
    if (pageText.length() > 6) { reply(400, "Invalid target page"); return; }
    for (size_t i = 0; i < pageText.length(); ++i) {
      if (pageText[i] < '0' || pageText[i] > '9') { reply(400, "Invalid target page"); return; }
      page = page * 10 + (pageText[i] - '0');
    }
  }
  if (workflow_.active() || queue_.size() || !dashboardTargetsIdle()) { reply(409, "Stop the active task before reading targets"); return; }
  const size_t total = dashboardToolTargetCount(kind.c_str());
  http_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  http_.send(200, "application/json", "");
  http_.sendContent("{\"kind\":\"" + kind + "\",\"targets\":[");
  size_t sent = 0;
  for (size_t index = size_t(page) * 40; index < total && sent < 40; ++index) {
    WorkflowTarget target;
    if (!dashboardToolTargetAt(kind.c_str(), index, target)) break;
    StaticJsonDocument<512> doc;
    doc["id"] = target.id; doc["name"] = target.name; doc["rssi"] = target.rssi;
    doc["channel"] = target.channel; doc["security"] = target.security;
    doc["packets"] = target.packets; doc["lastSeen"] = target.lastSeen;
    String entry; serializeJson(doc, entry);
    if (sent++) http_.sendContent(",");
    http_.sendContent(entry);
  }
  http_.sendContent("],\"total\":" + String(total) + ",\"limit\":40,\"page\":" + String(page) + "}");
  http_.sendContent("");
}

void DeviceDashboard::workflow() {
  if (!body_.valid()) { reply(413, "Task request exceeds 512 bytes"); return; }
  if (!toolSafeJson(body_.data())) { reply(400, "Null characters are not allowed in task inputs"); return; }
  DynamicJsonDocument doc(1024), response(1024);
  if (!doc.capacity() || !response.capacity()) { reply(503,"Insufficient memory for task request"); return; }
  if (deserializeJson(doc, body_.data(), body_.size()) || !doc.is<JsonObject>() || !doc["action"].is<const char*>()) {
    reply(400, "Expected a task action"); return;
  }
  const char* action = doc["action"];
  if (!strcmp(action, "cancel")) {
    if (!doc["id"].is<uint32_t>() || !workflow_.active() || doc["id"].as<uint32_t>() != workflow_.id) {
      reply(409, "That task is no longer active"); return;
    }
    endWorkflow(WorkflowState::Cancelled);
  } else if (!strcmp(action, "start")) {
    if (doc["kind"].is<const char*>() && !strcmp(doc["kind"].as<const char*>(), "tool")) {
      char error[161] = {};
      ToolRunner* incoming = new (std::nothrow) ToolRunner();
      if (!incoming) { reply(503, "Insufficient memory for this tool"); return; }
      const int code = incoming->prepare(doc.as<JsonObjectConst>(), error, sizeof(error));
      const bool busy = workflow_.active() || queue_.size() || !dashboardTargetsIdle();
      const bool auxiliary = workflow_.state == WorkflowState::Running &&
        (!strcmp(incoming->during(), "read") || (!strcmp(incoming->during(), "csi") && !strcmp(workflow_.kind,"tool") && !strcmp(tool_.check(), "csi")));
      const bool stopping = incoming->stop();
      if (busy && !auxiliary && !stopping) { incoming->clearSecrets(); delete incoming; reply(409, "Another task or manual operation is active"); return; }
      if (code) { incoming->clearSecrets(); delete incoming; reply(code, error); return; }
      if (auxiliary || stopping) {
        uint32_t sideCursor = dashboardSerial().logCursor();
        if (stopping) recover();
        else while (incoming->hasStep()) { toolDispatch_ = true; dashboardDispatch(String(incoming->nextStep())); toolDispatch_ = false; }
        incoming->clearSecrets(); delete incoming;
        workflowStatus(response.createNestedObject("workflow"));
        response["side"] = !stopping; response["logStart"] = sideCursor;
        String json; serializeJson(response,json); http_.send(202,"application/json",json); return;
      }
      if (*incoming->during() && !strcmp(incoming->during(), "csi")) { incoming->clearSecrets(); delete incoming; reply(409, "Start CSI motion before using this control"); return; }
      tool_ = *incoming; incoming->clearSecrets(); delete incoming;
      if (!workflow_.queueTool(doc["tool"] | "", toolSequenceBudget(tool_.timed(), doc["seconds"].as<int>()), doc["channel"].as<int>(), millis())) { reply(400,"Invalid tool parameters"); return; }
      workflow_.logStart = dashboardSerial().logCursor();
      timer_.clear(); workflowAt_ = millis(); workflowStopped_ = false;
      capturePath_[0] = 0; workflowError_[0] = 0;
    } else {
    if (!doc["kind"].is<const char*>() || !doc["seconds"].is<int>() || !doc["channel"].is<int>() ||
        (!doc["target"].isNull() && !doc["target"].is<const char*>())) {
      reply(400, "Expected task kind, whole seconds, channel, and optional target MAC"); return;
    }
    const char* kind = doc["kind"], *mac = doc["target"] | "";
    const int seconds = doc["seconds"], requestedChannel = doc["channel"];
    if (workflowValidate(kind, seconds, requestedChannel, mac)) {
      reply(400, "Use a supported task, 1 to 300 seconds, channel 0 to 14, and a target MAC when required"); return;
    }
    if (workflow_.active() || queue_.size() || !dashboardTargetsIdle()) { reply(409, "Another task or manual operation is active"); return; }
    WorkflowTarget selected;
    const bool hasTarget = *mac;
    if (hasTarget && !dashboardFindTarget(workflowBluetooth(kind), mac, selected)) {
      reply(404, "Target is no longer in the discovery list. Discover devices again."); return;
    }
    uint8_t channel = requestedChannel;
    if (hasTarget && !workflowBluetooth(kind) && (!channel || workflowSignal(kind) || workflowInspect(kind))) channel = selected.channel;
    if ((!strcmp(kind, "wifi_capture") || !strcmp(kind, "wifi_signal")) && (!channel || channel > 14)) {
      reply(400, "The target must have a supported 2.4 GHz channel"); return;
    }
    // Storage failure is retained as a failed accepted job, visible after reconnect.
    if (!workflow_.queue(kind, seconds, channel, hasTarget ? &selected : nullptr, millis())) {
      reply(400, "Invalid task parameters"); return;
    }
    timer_.clear(); workflowAt_ = millis(); workflowStopped_ = false;
    if (!strcmp(kind, "wifi_capture")) workflow_.count = 0;
    capturePath_[0] = 0; workflowError_[0] = 0;
    }
  } else { reply(400, "Task action must be start or cancel"); return; }
  workflowStatus(response.createNestedObject("workflow"));
  String json; serializeJson(response, json);
  http_.send(202, "application/json", json);
}

bool DeviceDashboard::manualAllowed(const char* command) {
  if (toolDispatch_) return true;
  if (access_.changing()) {
    if (!strcmp(command, "dashboard")) return true;
    Serial.println("[Dashboard] Wi-Fi password is changing; wait for recovery.");
    return false;
  }
  if (!workflow_.active()) return true;
  if (workflowManualAllowed(true, command)) {
    endWorkflow(WorkflowState::Cancelled);
  } else Serial.println("[Dashboard] Guided task active; stop it before manual commands.");
  return false;
}

void DeviceDashboard::endWorkflow(WorkflowState state, const char* error) {
  if (!workflow_.active()) return;
  workflow_.recover(millis()); workflowResult_ = state;
  workflowCopy(workflowError_, sizeof(workflowError_), error);
  workflowAt_ = millis();
}

void DeviceDashboard::stopWorkflow() {
  const bool capture = !strcmp(workflow_.kind, "wifi_capture") ||
    (!strcmp(workflow_.kind, "tool") && !strcmp(tool_.check(), "capture"));
  // Snapshot RSSI before shutdown clears the foxhunt state. Never traverse lists here.
  if (workflowSignal(workflow_.kind)) workflow_.rssi = dashboardSignalRssi();
  const bool idleManagement = !strcmp(workflow_.kind, "tool") && !tool_.timed() && dashboardTargetsIdle();
  if (!idleManagement) dashboardStop();
  dashboardWorkflowRestore();
  dashboardToolRestoreSelection();
  tool_.clearSecrets();
  if (dashboardTargetsIdle()) {
    if (workflowDiscover(workflow_.kind)) workflow_.count = dashboardTargetCount(workflowBluetooth(workflow_.kind));
    else if (workflow_.target[0] && !workflowSignal(workflow_.kind)) {
      WorkflowTarget target;
      if (dashboardFindTarget(workflowBluetooth(workflow_.kind), workflow_.target, target)) {
        workflow_.rssi = target.rssi; workflow_.count = target.packets;
      }
    }
  }
  if (capture && workflowResult_ == WorkflowState::Completed) {
    workflow_.count = dashboardCapturePackets();
    bool valid = dashboardCaptureHealthy() && capturePath_[0];
    File file = valid ? SPIFFS.open(capturePath_, FILE_READ) : File();
    uint8_t header[4] = {};
    valid = valid && file && !file.isDirectory() && file.size() >= 24 && file.read(header, 4) == 4 &&
      header[0] == 0xd4 && header[1] == 0xc3 && header[2] == 0xb2 && header[3] == 0xa1;
    file.close();
    if (valid) workflowCopy(workflow_.file, sizeof(workflow_.file), capturePath_);
    else {
      workflowResult_ = WorkflowState::Failed;
      workflowCopy(workflowError_, sizeof(workflowError_), "Capture was not saved. Check SavePCAP and available internal storage.");
    }
  }
  workflowStopped_ = true;
  if (!idleManagement || !listening_) restoreAP();
}

void DeviceDashboard::workflowLoop(uint32_t now) {
  if (toolLoop(now)) return;
  if (workflow_.state == WorkflowState::Queued && uint32_t(now - workflowAt_) >= 150) {
    if (!dashboardTargetsIdle()) { endWorkflow(WorkflowState::Failed, "Radio became busy before the task started"); return; }
    WorkflowTarget selected;
    const bool hasTarget = workflow_.target[0];
    // Re-resolve by identity at execution; a reordered list cannot change the target.
    if (hasTarget && !dashboardFindTarget(workflowBluetooth(workflow_.kind), workflow_.target, selected)) {
      endWorkflow(WorkflowState::Failed, "Target disappeared before the task started"); return;
    }
    if (workflowInspect(workflow_.kind)) {
      workflowCopy(workflow_.targetName, sizeof(workflow_.targetName), selected.name);
      workflow_.rssi = selected.rssi; workflow_.count = selected.packets; workflow_.channel = selected.channel;
      workflow_.running(now); workflow_.recover(now); workflow_.finish(WorkflowState::Completed);
      return;
    }
    if (!strcmp(workflow_.kind, "wifi_capture") && !dashboardCaptureReady()) {
      endWorkflow(WorkflowState::Failed, "Recording unavailable. Enable SavePCAP and keep at least 64 KiB of internal storage free."); return;
    }
    char previousPath[129] = {};
    dashboardCapturePath(previousPath, sizeof(previousPath));
    if (!dashboardStartWorkflow(workflow_.kind, workflow_.channel, hasTarget ? &selected : nullptr)) {
      endWorkflow(WorkflowState::Failed, "The requested radio operation could not start"); return;
    }
    workflowOperation_ = operation_;
    workflow_.running(millis());
    if (!strcmp(workflow_.kind, "wifi_capture") &&
        (!dashboardCapturePath(capturePath_, sizeof(capturePath_)) || !strcmp(previousPath, capturePath_)))
      endWorkflow(WorkflowState::Failed, "Could not create a new capture file. Check SavePCAP and internal storage.");
  }
  // Radio initialization can take time. A timestamp sampled before running()
  // would look like a full uint32 rollover and expire a newly started job.
  now = millis();
  if (workflow_.state == WorkflowState::Running) {
    if (dashboardMode() != dashboardWorkflowMode(workflow_.kind) || operation_ != workflowOperation_)
      endWorkflow(WorkflowState::Failed, "Radio operation changed unexpectedly");
    else if (!strcmp(workflow_.kind, "wifi_capture") && !dashboardCaptureHealthy())
      endWorkflow(WorkflowState::Failed, "Capture recording stopped. Internal storage may be full or unavailable.");
    else if (workflow_.expired(now)) endWorkflow(WorkflowState::Completed);
    else if (workflowSignal(workflow_.kind)) workflow_.rssi = dashboardSignalRssi();
  }
  now = millis();
  if (workflow_.state == WorkflowState::Recovering) {
    if (!workflowStopped_ && uint32_t(now - workflowAt_) >= 150) stopWorkflow();
    if (workflowStopped_ && listening_) workflow_.finish(workflowResult_, workflowError_);
  }
}

void DeviceDashboard::changeAccess() {
  if (workflow_.active() || queue_.size() || !dashboardTargetsIdle()) {
    reply(409, "Stop the active task before changing the Wi-Fi password."); return;
  }
  if (!toolSafeJson(body_.data()) || memchr(body_.data(), 0, body_.size())) {
    reply(400, "Null characters are not allowed in passwords."); return;
  }
  StaticJsonDocument<256> doc;
  if (!body_.valid() || deserializeJson(doc, body_.data(), body_.size()) || !doc["password"].is<const char*>()) {
    reply(400, "Expected a new password."); return;
  }
  const char* value = doc["password"];
  const size_t length = strlen(value);
  if (!DashboardAccess::valid(value, length)) {
    reply(400, "Use 8 to 63 ASCII characters with no leading or trailing spaces."); return;
  }
  if (!access_.canChange(value, length)) {
    reply(400, "Choose a different password; the setup password is reserved."); return;
  }
  Preferences prefs;
  if (!prefs.begin("dashboard", false)) { reply(500, "Could not open credential storage."); return; }
  const bool saved = prefs.putString("password", value) == length;
  prefs.end();
  if (!saved) { reply(500, "Could not save password; the current password is unchanged."); return; }
  access_.stage(value, length, millis());
  reply(200, "Password saved. Reconnect Wi-Fi using the new password.");
}

void DeviceDashboard::command() {
  if (!body_.valid()) { reply(413, "Command request too large"); return; }
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, body_.data(), body_.size()) || !doc["command"].is<const char*>() ||
      !doc["seconds"].is<unsigned int>() || doc["seconds"].as<unsigned int>() > 3600) {
    reply(400, "Expected a command and seconds from 0 to 3600"); return;
  }
  const char* text = doc["command"];
  if (!DashboardQueue::valid(text)) { reply(400, "Use one command, 1 to 256 bytes, with no control characters"); return; }
  if (!workflowManualAllowed(workflow_.active(), text)) { reply(409, "A guided task is active. Stop it before using manual commands."); return; }
  if (workflow_.active()) {
    endWorkflow(WorkflowState::Cancelled);
    reply(202, "Cancelling task and restoring control Wi-Fi"); return;
  }
  if (!queue_.push(text, doc["seconds"].as<uint16_t>())) { reply(429, "Command queue full; wait for it to drain"); return; }
  pendingAt_ = millis();
  reply(202, "Queued for execution; results appear in the console");
}

void DeviceDashboard::files(bool download) {
  if (http_.arg("storage") != "spiffs") { reply(503, "This bare-board build has no SD card"); return; }
  if (download) {
    if (dashboardMode() != 0) { reply(409, "Stop the operation before downloading a capture"); return; }
    String path = http_.arg("path");
    if (!dashboardSafePath(path.c_str())) { reply(400, "Only capture and log files may be downloaded"); return; }
    File file = SPIFFS.open(path, FILE_READ);
    if (!file || file.isDirectory()) { reply(404, "File not found"); return; }
    http_.sendHeader("Content-Disposition", "attachment");
    http_.streamFile(file, "application/octet-stream");
    file.close();
    return;
  }
  // Stream directory entries instead of allocating a directory-sized JSON document.
  http_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  http_.send(200, "application/json", "");
  http_.sendContent("{\"files\":[");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  size_t count = 0;
  while (file && count < 100) {
    String path = file.path();
    if (!file.isDirectory() && dashboardSafePath(path.c_str())) {
      StaticJsonDocument<256> doc;
      doc["path"] = path;
      doc["size"] = file.size();
      String entry; serializeJson(doc, entry);
      if (count++) http_.sendContent(",");
      http_.sendContent(entry);
    }
    file.close(); file = root.openNextFile();
  }
  file.close(); root.close();
  http_.sendContent("],\"limit\":100}");
  http_.sendContent("");
}

void DeviceDashboard::deleteFile() {
  if (dashboardMode() != 0) { reply(409, "Stop the operation before deleting a capture"); return; }
  StaticJsonDocument<384> doc;
  if (!body_.valid() || deserializeJson(doc, body_.data(), body_.size()) || !doc["path"].is<const char*>()) {
    reply(400, "Expected a capture file path"); return;
  }
  const char* path = doc["path"];
  if (!dashboardSafePath(path)) { reply(400, "Only capture and log files may be deleted"); return; }
  if (!SPIFFS.exists(path)) { reply(404, "File not found"); return; }
  if (!SPIFFS.remove(path)) { reply(500, "Could not delete file"); return; }
  reply(200, "File deleted");
}

void DeviceDashboard::loop() {
  if (listening_) http_.handleClient();
  const uint32_t now = millis();
  if (access_.apply(now)) {
    // Disconnect clients using the old WPA key before recreating the AP.
    suspend();
    WiFi.softAPdisconnect(false);
    restoreAP();
  }
  workflowLoop(now);
  if (timer_.expired(now)) {
    Serial.println("[Dashboard] Run timer ended; restoring control Wi-Fi");
    recover();
  }
  DashboardCommand command;
  // Give the accepted response time to reach the browser before changing the radio.
  if (!workflow_.active() && uint32_t(now - pendingAt_) >= 150 && queue_.pop(command)) {
    // Every operation has a timer, even modes that do not update currentScanMode.
    if (strcmp(command.text, "stopscan") == 0 || strcmp(command.text, "dashboard recover") == 0) {
      recover();
    } else {
      const uint32_t before = operation_;
      const uint8_t previousMode = dashboardMode();
      dashboardDispatch(String(command.text));
      if (before == operation_ && previousMode != dashboardMode()) operationChanged();
      timer_.afterCommand(millis(), command.seconds, before, operation_, dashboardMode() != 0);
    }
  }
  if (dashboardMode() == 0 &&
      (!retryAt_ || int32_t(now - retryAt_) >= 0)) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (!listening_ || !(mode & WIFI_MODE_AP) || WiFi.softAPSSID() != ssid_) restoreAP();
  }
}
#endif
