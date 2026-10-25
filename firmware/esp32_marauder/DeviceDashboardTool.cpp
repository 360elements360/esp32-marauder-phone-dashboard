#include "DeviceDashboard.h"
#ifdef MARAUDER_DASHBOARD
#include "DashboardBridge.h"
#include "DashboardSerial.h"
bool DeviceDashboard::toolLoop(uint32_t now) {
  if(strcmp(workflow_.kind,"tool"))return false;
  if(workflow_.state==WorkflowState::Queued && uint32_t(now-workflowAt_)>=150) {
    if(!dashboardTargetsIdle()) {endWorkflow(WorkflowState::Failed,"Radio became busy before the tool started");return true;}
    char error[161]={};
    if(tool_.revalidate(error,sizeof(error))){endWorkflow(WorkflowState::Failed,error);return true;}
    if(!strcmp(tool_.check(),"capture") && !dashboardCaptureReady()) {endWorkflow(WorkflowState::Failed,"Recording unavailable. Enable SavePCAP and keep 64 KiB free.");return true;}
    if(tool_.timed())dashboardToolRadioPrepare(workflow_.channel,tool_.channelPolicy());
    workflow_.running(millis());workflowOperation_=operation_;toolStepAt_=millis()-150;
  }
  if(workflow_.state==WorkflowState::Running) {
    if(operation_!=workflowOperation_) {endWorkflow(WorkflowState::Failed,"Another operation interrupted the tool");return true;}
    if(tool_.hasStep() && uint32_t(millis()-toolStepAt_)>=150) {
      if(workflow_.expired(millis())){endWorkflow(WorkflowState::Failed,"Tool deadline elapsed before its commands finished");return true;}
      const char* step=tool_.nextStep();
      toolDispatch_=true;dashboardDispatch(String(step));toolDispatch_=false;
      workflowOperation_=operation_;toolStepAt_=millis();
      if(!dashboardCommandSucceeded()) {endWorkflow(WorkflowState::Failed,"The device rejected a tool command. Read its output.");return true;}
      if(!tool_.timed() && workflow_.expired(millis())) {endWorkflow(WorkflowState::Failed,"The command sequence exceeded its 30-second deadline. Read its output.");return true;}
      if(!tool_.hasStep()) {
        if(!tool_.modeExpected(dashboardMode()) || !dashboardToolCheck(tool_.check())) {endWorkflow(WorkflowState::Failed,"The requested operation could not start. Read the device output.");return true;}
        if(!strcmp(tool_.check(),"capture") && !dashboardCapturePath(capturePath_,sizeof(capturePath_))) {endWorkflow(WorkflowState::Failed,"Capture file was not created");return true;}
        // Give a timed radio task its full requested dwell after initialization.
        if(tool_.timed())workflow_.running(millis());
        if(!tool_.timed())endWorkflow(WorkflowState::Completed);
      }
    } else if(!tool_.hasStep()) {
      if(!tool_.modeExpected(dashboardMode()) || !dashboardToolCheck(tool_.check()))endWorkflow(WorkflowState::Failed,"The requested operation stopped unexpectedly. Read the device output.");
      else if(workflow_.expired(millis()))endWorkflow(WorkflowState::Completed);
    }
  }
  if(workflow_.state==WorkflowState::Recovering) {
    if(!workflowStopped_ && uint32_t(millis()-workflowAt_)>=150)stopWorkflow();
    if(workflowStopped_ && listening_)workflow_.finish(workflowResult_,workflowError_);
  }
  return true;
}
#endif
