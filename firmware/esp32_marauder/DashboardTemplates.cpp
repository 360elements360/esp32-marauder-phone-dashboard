#ifndef DASHBOARD_TEMPLATES_NATIVE_TEST
#include "configs.h"
#endif
#ifdef MARAUDER_DASHBOARD
#include "DashboardTemplates.h"
#ifndef DASHBOARD_TEMPLATES_NATIVE_TEST
#include "DashboardBridge.h"
#include <SPIFFS.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
namespace {
constexpr size_t kTemplateLimit=8192, kChunkLimit=256, kTemplateReserve=64*1024;
constexpr const char* kPrivateTemplate="/.dashboard-template.tmp";
struct TemplateUpload {
  char path[28]={};
  uint32_t total=0, offset=0, lastAt=0;
  bool active=false, complete=false;
} upload;
bool templateReply(int code,const char* text,int& httpCode,char* message,size_t capacity) {
  httpCode=code; if(message&&capacity) snprintf(message,capacity,"%s",text); return code==200;
}
bool templatePath(const char* source,char* output) {
  if(!source) return false;
  if(*source=='/') ++source;
  const size_t length=strlen(source);
  if(length<6||length>26||strcmp(source+length-5,".html")) return false;
  for(size_t i=0;i<length-5;++i) {
    unsigned char c=static_cast<unsigned char>(source[i]);
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return false;
  }
  snprintf(output,28,"/%s",source); return true;
}
bool templateUtf8(const char* text,size_t bytes) {
  for(size_t i=0;i<bytes;) {
    uint8_t first=static_cast<uint8_t>(text[i++]);
    if(first<0x80) continue;
    uint32_t codepoint; unsigned tails; uint32_t minimum;
    if(first>=0xc2&&first<=0xdf) { codepoint=first&0x1f; tails=1; minimum=0x80; }
    else if(first>=0xe0&&first<=0xef) { codepoint=first&0x0f; tails=2; minimum=0x800; }
    else if(first>=0xf0&&first<=0xf4) { codepoint=first&0x07; tails=3; minimum=0x10000; }
    else return false;
    if(i+tails>bytes) return false;
    while(tails--) { uint8_t next=static_cast<uint8_t>(text[i++]); if((next&0xc0)!=0x80) return false; codepoint=(codepoint<<6)|(next&0x3f); }
    if(codepoint<minimum||codepoint>0x10ffff||(codepoint>=0xd800&&codepoint<=0xdfff)) return false;
  }
  return true;
}
bool templateSpace(size_t bytes) {
  size_t total=SPIFFS.totalBytes(),used=SPIFFS.usedBytes();
  return total>0&&used<=total&&total-used>=kTemplateReserve+bytes+4096;
}
void templateAbort() { upload.active=false; upload.complete=false; SPIFFS.remove(kPrivateTemplate); }
void templateBackup(char* output) { snprintf(output,32,"%s.bak",upload.path); }
bool templateInstall() {
  char previousPath[32]; templateBackup(previousPath);
  if(SPIFFS.exists(previousPath)&&!SPIFFS.remove(previousPath)) return false;
  bool previous=SPIFFS.exists(upload.path);
  if(previous&&!SPIFFS.rename(upload.path,previousPath)) return false;
  if(!SPIFFS.rename(kPrivateTemplate,upload.path)) {
    if(previous) SPIFFS.rename(previousPath,upload.path);
    return false;
  }
  SPIFFS.remove(previousPath); return true;
}
}
const char* dashboardTemplatePath() { return upload.path; }
size_t dashboardTemplateOffset() { return upload.offset; }
bool dashboardTemplateComplete() { return upload.complete; }
bool dashboardWriteTemplate(JsonObjectConst request,int& httpCode,char* message,size_t capacity) {
  if(!dashboardTargetsIdle()) return templateReply(409,"Stop active radio tasks before uploading a template",httpCode,message,capacity);
  const uint32_t now=millis();
  if(upload.active&&uint32_t(now-upload.lastAt)>=60000u) templateAbort();
  if(request.size()!=5||!request["path"].is<const char*>()||!request["data"].is<const char*>()
    ||!request["offset"].is<uint32_t>()||!request["total"].is<uint32_t>()||!request["final"].is<bool>())
    return templateReply(400,"Expected path, offset, data, final and total with their exact types",httpCode,message,capacity);
  char path[28];
  if(!templatePath(request["path"].as<const char*>(),path))
    return templateReply(400,"Use a template filename of at most 26 characters: letters, digits, underscore or hyphen followed by .html",httpCode,message,capacity);
  const char* data=request["data"].as<const char*>(); const size_t length=strlen(data);
  const uint32_t offset=request["offset"].as<uint32_t>(),total=request["total"].as<uint32_t>();
  const bool final=request["final"].as<bool>();
  if(total>kTemplateLimit||length>kChunkLimit||offset>total||length>total-offset||!templateUtf8(data,length)
    ||(final?offset+length!=total:(!length||offset+length>=total)))
    return templateReply(400,"Invalid UTF-8 chunk, byte count or final flag; limit 256 bytes per chunk and 8192 bytes total",httpCode,message,capacity);
  if(offset) {
    if(!upload.active||strcmp(upload.path,path)||upload.offset!=offset||upload.total!=total)
      return templateReply(409,"Upload offset, filename or total does not match; restart at offset zero",httpCode,message,capacity);
  } else {
    if(!templateSpace(total)) return templateReply(507,"Internal storage unavailable or 64 KiB settings reserve reached",httpCode,message,capacity);
    templateAbort();
    if(SPIFFS.exists(kPrivateTemplate)) return templateReply(500,"Could not clear the private upload file",httpCode,message,capacity);
    snprintf(upload.path,sizeof(upload.path),"%s",path); upload.total=total; upload.offset=0;
    char previousPath[32]; templateBackup(previousPath);
    // Recover the previous template after a reset between the two final renames.
    if(!SPIFFS.exists(path)&&SPIFFS.exists(previousPath)&&!SPIFFS.rename(previousPath,path))
      return templateReply(500,"Could not recover the previous template backup",httpCode,message,capacity);
    upload.active=true; upload.complete=false;
  }
  if(!templateSpace(length)) { templateAbort(); return templateReply(507,"Settings storage reserve reached; restart the upload",httpCode,message,capacity); }
  if(offset&&!SPIFFS.exists(kPrivateTemplate)) { templateAbort(); return templateReply(409,"Partial template missing; restart at offset zero",httpCode,message,capacity); }
  File file=SPIFFS.open(kPrivateTemplate,offset?"a":FILE_WRITE);
  if(!file||file.size()!=offset) {
    if(file) file.close(); templateAbort();
    return templateReply(507,"Could not open the private upload file; restart at offset zero",httpCode,message,capacity);
  }
  bool written=file.write(reinterpret_cast<const uint8_t*>(data),length)==length;
  file.flush(); written=written&&file.size()==offset+length; file.close();
  if(!written) { templateAbort(); return templateReply(507,"Template write failed; previous template preserved",httpCode,message,capacity); }
  upload.offset=offset+length; upload.lastAt=now;
  if(final) {
    if(!templateInstall()) { templateAbort(); return templateReply(500,"Could not install template; previous template retained in the file or its backup",httpCode,message,capacity); }
    upload.active=false; upload.complete=true;
  }
  return templateReply(200,final?"Template uploaded":"Template chunk accepted",httpCode,message,capacity);
}
#endif
