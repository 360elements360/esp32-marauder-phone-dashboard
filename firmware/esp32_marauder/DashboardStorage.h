#pragma once
#include <stddef.h>
// Called only from the main loop. Both functions also refuse active radio callbacks.
// List loads replace the requested list only after the complete file validates.
bool dashboardSaveTargetList(bool ssidList, char* error = nullptr, size_t capacity = 0);
bool dashboardLoadTargetList(bool ssidList, char* error = nullptr, size_t capacity = 0);
bool dashboardStorageSucceeded();
