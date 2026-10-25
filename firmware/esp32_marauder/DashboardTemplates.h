#pragma once
#include <stddef.h>
#include <ArduinoJson.h>
// Caller authenticates, verifies same origin, bounds raw JSON to 512 bytes, and
// rejects encoded NUL before parsing (ArduinoJson 6.18 loses its original length).
// Fields: path, offset, data (<=256 decoded UTF-8 bytes), final, total (<=8192).
// The basename is [A-Za-z0-9_-]+.html, <=26 characters, with optional leading '/'.
bool dashboardWriteTemplate(JsonObjectConst request, int& httpCode, char* message, size_t capacity);
const char* dashboardTemplatePath();
size_t dashboardTemplateOffset();
bool dashboardTemplateComplete();
