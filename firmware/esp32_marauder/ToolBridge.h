#pragma once
#include "ToolCore.h"
#include <ArduinoJson.h>
bool dashboardToolTargetKind(const char* kind);
size_t dashboardToolTargetCount(const char* kind);
bool dashboardToolTargetAt(const char* kind, size_t index, WorkflowTarget& target);
bool dashboardToolResolve(const char* kind, const char* id, WorkflowTarget& target, int& index, int& apIndex);
bool dashboardToolSelect(const char* kind, const char* indices);
void dashboardToolRestoreSelection();
bool dashboardToolCheck(const char* check);
class ToolRunner {
  char request_[513] = {};
  const char* definition_ = nullptr;
  char steps_[8][257] = {};
  uint8_t modes_[16] = {}, modeCount_ = 0, stepCount_ = 0, next_ = 0;
  char check_[17] = {};
  char during_[12] = {};
  bool stop_ = false;
  bool timed_ = false;
  bool channelPolicy_ = false;
public:
  int prepare(JsonObjectConst request, char* error, size_t capacity, bool execution = false);
  bool hasStep() const { return next_ < stepCount_; }
  const char* nextStep() { return hasStep() ? steps_[next_++] : nullptr; }
  bool timed() const { return timed_; }
  bool channelPolicy() const { return channelPolicy_; }
  bool modeExpected(uint8_t mode) const { for (uint8_t i=0;i<modeCount_;++i) if(modes_[i]==mode)return true; return modeCount_==0; }
  const char* check() const { return check_; }
  const char* during() const { return during_; }
  bool stop() const { return stop_; }
  int revalidate(char* error, size_t capacity);
  void clearSecrets() { memset(request_, 0, sizeof(request_)); memset(steps_, 0, sizeof(steps_)); }
};
