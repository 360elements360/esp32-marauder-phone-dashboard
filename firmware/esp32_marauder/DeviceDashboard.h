#pragma once
#include "configs.h"
#ifdef MARAUDER_DASHBOARD
#include "DashboardCore.h"
#include "DashboardAccess.h"
#include "WorkflowCore.h"
#include "ToolBridge.h"
#include <ArduinoJson.h>
class DashboardHttpServer;

class DeviceDashboard {
  DashboardHttpServer& http_;
  DashboardQueue queue_;
  DashboardBody body_;
  DashboardTimer timer_;
  WorkflowCore workflow_;
  ToolRunner tool_;
  bool toolDispatch_ = false;
  uint32_t toolStepAt_ = 0;
  WorkflowState workflowResult_ = WorkflowState::Completed;
  char workflowError_[161] = {}, capturePath_[129] = {};
  uint32_t workflowAt_ = 0, workflowOperation_ = 0;
  bool workflowStopped_ = false;
  DashboardAccess access_;
  char ssid_[24] = {}, logChunk_[1025] = {};
  uint32_t boot_ = 0, pendingAt_ = 0, retryAt_ = 0, operation_ = 0;
  bool listening_ = false;
  bool authorized(bool mutation = false);
  void reply(int code, const char* message);
  void status();
  void logs();
  void command();
  void changeAccess();
  void targets();
  void workflow();
  void workflowStatus(JsonObject object);
  void workflowLoop(uint32_t now);
  bool toolLoop(uint32_t now);
  void endWorkflow(WorkflowState state, const char* error = "");
  void stopWorkflow();
  void bodyChunk();
  void files(bool download);
  void deleteFile();
  void templateUpload();
  void restoreAP();
public:
  DeviceDashboard();
  void begin();
  void loop();
  void recover();
  void suspend();
  void credentials();
  void diagnostics();
  void operationChanged() { ++operation_; timer_.clear(); }
  bool manualAllowed(const char* command);
  bool internalCommand() const { return toolDispatch_; }
};
extern DeviceDashboard dashboard_obj;
#endif
