#pragma once
#include <Arduino.h>
#include "WorkflowCore.h"
// Keep the synchronous HTTP types separate from the portal's async HTTP enums.
uint8_t dashboardMode();
uint8_t dashboardChannel();
void dashboardDispatch(const String& command);
bool dashboardCommandSucceeded();
void dashboardCommandFailed();
void dashboardStop();
bool dashboardTargetsIdle();
size_t dashboardTargetCount(bool bluetooth);
bool dashboardTargetAt(bool bluetooth, size_t index, WorkflowTarget& target);
bool dashboardFindTarget(bool bluetooth, const char* mac, WorkflowTarget& target);
uint8_t dashboardWorkflowMode(const char* kind);
bool dashboardStartWorkflow(const char* kind, uint8_t channel, const WorkflowTarget* target);
void dashboardWorkflowRestore();
void dashboardToolRadioPrepare(uint8_t channel, bool applyChannel);
bool dashboardCaptureReady();
bool dashboardCaptureHealthy();
uint32_t dashboardCapturePackets();
bool dashboardCapturePath(char* path, size_t capacity);
int dashboardSignalRssi();
