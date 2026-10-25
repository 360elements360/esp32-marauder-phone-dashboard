#pragma once
#include "WorkflowCore.h"
#include <stdlib.h>
struct ToolArgument {
  char name[33] = {}, value[129] = {}, indices[97] = {};
  int apIndex = -1;
  bool quoted = false, target = false;
};
inline bool toolSafeJson(const char* text) { return text && !strstr(text,"\\u0000"); }
inline uint16_t toolSequenceBudget(bool timed, uint16_t seconds) { return timed ? seconds : 30; }
inline bool toolSafeText(const char* text, size_t maximum) {
  if (!text || strlen(text) > maximum) return false;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
    if (*p < 32 || *p == 127 || *p == '"') return false;
  return true;
}
inline bool toolValidIp(const char* text) {
  if (!text || !*text) return false;
  for (unsigned n = 0; n < 4; ++n) {
    unsigned value = 0, digits = 0;
    while (*text >= '0' && *text <= '9') { value = value * 10 + (*text++ - '0'); if (++digits > 3 || value > 255) return false; }
    if (!digits || (n < 3 ? *text++ != '.' : *text != 0)) return false;
  }
  return true;
}
inline bool toolPublicHtml(const char* text) {
  if (!text || strlen(text)<6 || strlen(text)>96 || strstr(text,"..")) return false;
  const size_t n=strlen(text);
  if (strcmp(text+n-5,".html")) return false;
  for (const char* p=text;*p;++p) if (!( (*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || *p=='_' || *p=='-' || *p=='.')) return false;
  return true;
}
// Two independently seeded hashes plus byte length bind configuration identities
// to their content. An index never forms part of a browser-visible identity.
inline void toolContentIdentity(const char* kind, const char* content, char id[49]) {
  uint32_t a = 2166136261u, b = 5381u;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(content); *p; ++p) { a = (a ^ *p) * 16777619u; b = ((b << 5) + b) ^ *p; }
  snprintf(id, 49, "%s:%08lx%08lx:%u", kind, (unsigned long)a, (unsigned long)b, unsigned(strlen(content)));
}
inline bool toolContentMatches(const char* kind, const char* content, const char* identity) {
  char canonical[49]; toolContentIdentity(kind, content, canonical); return identity && !strcmp(canonical, identity);
}
inline bool toolRender(const char* recipe, const ToolArgument* args, size_t count, int channel, int seconds, char* output, size_t capacity) {
  size_t used = 0;
  auto append = [&](const char* value) { size_t n = strlen(value); if (used + n >= capacity) return false; memcpy(output + used, value, n); used += n; output[used] = 0; return true; };
  if (!recipe || !capacity) return false;
  output[0] = 0;
  while (*recipe) {
    if (*recipe != '{') { char literal[2] = {*recipe++, 0}; if (!append(literal)) return false; continue; }
    const char* end = strchr(++recipe, '}');
    if (!end || end - recipe > 47) return false;
    char key[48] = {}; memcpy(key, recipe, end - recipe); recipe = end + 1;
    char number[16];
    if (!strcmp(key, "channel") || !strcmp(key, "seconds")) { snprintf(number, sizeof(number), "%d", !strcmp(key,"channel") ? channel : seconds); if (!append(number)) return false; continue; }
    char* suffix = strchr(key, '.'); if (suffix) *suffix++ = 0;
    const ToolArgument* arg = nullptr;
    for (size_t i = 0; i < count; ++i) if (!strcmp(key, args[i].name)) { arg = &args[i]; break; }
    if (!arg) return false;
    if (suffix) {
      if (!arg->target) return false;
      if (!strcmp(suffix, "index")) { if (!arg->indices[0] || !append(arg->indices)) return false; }
      else if (!strcmp(suffix, "apIndex")) { if (arg->apIndex < 0) return false; snprintf(number,sizeof(number),"%d",arg->apIndex); if (!append(number)) return false; }
      else return false;
    } else {
      if (!toolSafeText(arg->value, sizeof(arg->value)-1)) return false;
      if (arg->quoted && !append("\"")) return false;
      if (!append(arg->value)) return false;
      if (arg->quoted && !append("\"")) return false;
    }
  }
  return used > 0 && used <= 256;
}
