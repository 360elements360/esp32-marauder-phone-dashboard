#include "../firmware/esp32_marauder/WorkflowCore.h"
#include "../firmware/esp32_marauder/DashboardCore.h"
#include <assert.h>
#include <stdio.h>

int main() {
  uint8_t beacon[80] = {}; beacon[36] = 0; beacon[37] = 3;
  memcpy(beacon + 38, "net", 3);
  const uint8_t* name = nullptr; size_t nameLength = 0;
  assert(workflowBeaconSsid(beacon, 41, name, nameLength));
  assert(nameLength == 3 && !memcmp(name, "net", 3));
  for (size_t length = 0; length < 41; ++length)
    assert(!workflowBeaconSsid(beacon, length, name, nameLength));
  assert(!workflowBeaconSsid(nullptr, 80, name, nameLength));
  beacon[37] = 33;
  assert(!workflowBeaconSsid(beacon, 80, name, nameLength));
  beacon[37] = 32;
  assert(workflowBeaconSsid(beacon, 70, name, nameLength) && nameLength == 32);
  beacon[37] = 0;
  assert(workflowBeaconSsid(beacon, 38, name, nameLength) && nameLength == 0);
  beacon[36] = 1; beacon[37] = 1; beacon[38] = 1; beacon[39] = 0; beacon[40] = 1; beacon[41] = 'a';
  assert(workflowBeaconSsid(beacon, 42, name, nameLength) && nameLength == 1 && name[0] == 'a');
  assert(!workflowBeaconSsid(beacon, 41, name, nameLength));
  const WorkflowModes modes = {0, 49, 10, 11, 12, 13};
  assert(workflowRadioMode("wifi_discover", modes) == modes.wifiDiscovery);
  assert(workflowRadioMode("ble_discover", modes) == modes.bleDiscovery);
  assert(workflowRadioMode("wifi_signal", modes) == modes.wifiSignal);
  assert(workflowRadioMode("ble_signal", modes) == modes.bleSignal);
  assert(workflowRadioMode("wifi_capture", modes) == modes.wifiCapture);
  assert(workflowRadioMode("wifi_inspect", modes) == modes.idle);
  assert(workflowRadioMode("ble_inspect", modes) == modes.idle);
  assert(workflowChannelHopAllowed(-1, false, false)); // Legacy AP/STA ignores saved preference.
  assert(!workflowChannelHopAllowed(0, true, false)); // Guided fixed channel overrides it.
  assert(workflowChannelHopAllowed(1, false, false)); // Guided discovery visits all channels.
  assert(!workflowChannelHopAllowed(-1, false, true));
  assert(workflowChannelHopAllowed(-1, true, true));
  // The control AP initializes Wi-Fi without running a target-list callback.
  assert(workflowRadioIdle(0, false, false, false, false, false));
  assert(!workflowRadioIdle(1, false, false, false, false, false));
  assert(!workflowRadioIdle(0, true, false, false, false, false));
  assert(!workflowRadioIdle(0, false, true, false, false, false));
  assert(!workflowRadioIdle(0, false, false, true, false, false));
  assert(!workflowRadioIdle(0, false, false, false, true, false));
  assert(!workflowRadioIdle(0, false, false, false, false, true));
  WorkflowCore job;
  assert(job.id == 0 && !job.active() && !strcmp(job.stateName(), "idle"));
  assert(workflowValidate("wifi_discover", 0, 0, "") == 400);
  assert(workflowValidate(nullptr, 1, 0, "") == 400);
  assert(workflowValidate("wifi_discover", -1, 0, "") == 400);
  assert(workflowValidate("wifi_capture", 1, 1, "") == 0);
  assert(workflowValidate("wifi_inspect", 1, 0, "aa:bb:00:11:22:33") == 0);
  assert(workflowValidate("wifi_discover", 301, 0, "") == 400);
  assert(workflowValidate("wifi_discover", 300, 14, "") == 0);
  assert(workflowValidate("wifi_discover", 1, 15, "") == 400);
  assert(workflowValidate("other", 1, 0, "") == 400);
  assert(workflowValidate("ble_signal", 1, 0, "") == 400);
  assert(workflowValidate("wifi_capture", 1, 0, "") == 400);
  assert(workflowValidate("wifi_capture", 1, 0, "AA:bb:00:11:22:33") == 0);
  assert(workflowValidate("wifi_signal", 1, 0, "0:bb:00:11:22:33") == 400);
  uint8_t mac[6]; char canonical[18];
  assert(workflowParseMac("AA:bb:00:11:22:33", mac));
  assert(!workflowParseMac("aa:bb:00:11:22:3z", mac));
  assert(!workflowParseMac("aa-bb-00-11-22-33", mac));
  assert(!workflowParseMac("aa:bb:00:11:22:33x", mac));
  assert(workflowParseMac("AA:bb:00:11:22:33", mac));
  workflowFormatMac(mac, canonical);
  assert(!strcmp(canonical, "aa:bb:00:11:22:33"));
  WorkflowTarget targets[2];
  strcpy(targets[0].id, "10:00:00:00:00:01");
  strcpy(targets[1].id, canonical);
  assert(workflowTargetMatches(targets[1], mac));
  WorkflowTarget reordered = targets[1]; targets[1] = targets[0]; targets[0] = reordered;
  assert(workflowTargetMatches(targets[0], mac));
  assert(!workflowTargetMatches(targets[1], mac));
  assert(job.queue("wifi_signal", 2, 6, &targets[0], 0xfffffff0U));
  assert(job.id == 1 && job.active() && !strcmp(job.stateName(), "queued"));
  assert(!job.queue("wifi_discover", 1, 0, nullptr, 10));
  assert(!job.cancel(job.id + 1, 10));
  assert(!workflowManualAllowed(job.active(), "sniffbeacon"));
  assert(workflowManualAllowed(job.active(), "stopscan"));
  assert(workflowManualAllowed(job.active(), "dashboard recover"));
  assert(workflowManualAllowed(false, "anything"));
  job.running(0xfffffff0U);
  assert(job.remaining(984) == 1 && job.elapsed(984) == 1);
  assert(!job.expired(984));
  assert(job.expired(1984));
  job.recover(1984);
  assert(job.active() && !strcmp(job.stateName(), "recovering"));
  job.finish(WorkflowState::Completed);
  assert(!job.active() && job.elapsed(8000) == 2 && job.remaining(8000) == 0);
  assert(job.queue("ble_discover", 300, 0, nullptr, 10));
  assert(job.id == 2 && job.target[0] == 0 && job.error[0] == 0);
  assert(job.cancel(2, 20));
  assert(!strcmp(job.stateName(), "recovering"));
  job.finish(WorkflowState::Cancelled);
  assert(!strcmp(job.stateName(), "cancelled"));
  assert(!job.cancel(2, 30));
  assert(job.queue("wifi_capture", 1, 6, nullptr, 40));
  job.running(50); job.recover(70);
  job.finish(WorkflowState::Failed, "storage full");
  assert(!strcmp(job.error, "storage full") && job.file[0] == 0);
  assert(job.queue("wifi_capture", 1, 6, nullptr, 80));
  assert(job.error[0] == 0 && job.file[0] == 0);
  size_t requestLength = 0;
  assert(dashboardInspectHttp("POST /api/workflow HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}", requestLength) == 200);
  assert(requestLength > 2);
  assert(dashboardInspectHttp("POST /api/workflow HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 513\r\n\r\n", requestLength) == 413);
  assert(dashboardInspectHttp("POST /api/workflow HTTP/1.1\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n\r\n", requestLength) == 400);
  assert(dashboardInspectHttp("POST /api/workflow HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}", requestLength) == 400);
  assert(dashboardInspectHttp("POST /api/workflow HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 2\r\n\r\n{}", requestLength) == 415);
  puts("workflow core: validation, transitions, rollover, cancellation, MAC identity and arbitration passed");
}
