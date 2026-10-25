#define DASHBOARD_TEMPLATES_NATIVE_TEST
#define MARAUDER_DASHBOARD
#define ARDUINOJSON_ENABLE_STD_STRING 0
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#define ARDUINOJSON_ENABLE_STRING_VIEW 0
#include "storage_platform.h"
#include "../.deps/ArduinoJson/src/ArduinoJson.h"
static uint32_t nowMs=0;
static uint32_t millis() { return nowMs; }
#include "../firmware/esp32_marauder/DashboardTemplates.cpp"
#include <assert.h>
static bool send(const char* path,uint32_t offset,const char* data,bool final,uint32_t total,int& code) {
  StaticJsonDocument<768> doc;
  doc["path"]=path; doc["offset"]=offset; doc["data"]=data; doc["final"]=final; doc["total"]=total;
  char message[160]; return dashboardWriteTemplate(doc.as<JsonObjectConst>(),code,message,sizeof(message));
}
static void existing(const char* value) {
  File f=SPIFFS.open("/custom.html",FILE_WRITE); assert(f);
  assert(f.write(reinterpret_cast<const uint8_t*>(value),strlen(value))==strlen(value)); f.close();
}
static void assertContent(const char* path,const char* value) {
  File f=SPIFFS.open(path,FILE_READ); assert(f); assert(f.size()==strlen(value));
  for(size_t i=0;i<strlen(value);++i) assert(f.read()==static_cast<unsigned char>(value[i])); f.close();
}
int main() {
  yield();
  SPIFFS.remove("/custom.html"); SPIFFS.remove("/custom.html.bak"); int code;
  existing("old page");
  assert(send("custom.html",0,"<h1>",false,10,code)&&code==200);
  assertContent("/custom.html","old page");
  assert(dashboardTemplateOffset()==4&&!dashboardTemplateComplete());
  assert(!send("custom.html",3,"wrong!!",true,10,code)&&code==409);
  assert(!send("other.html",4,"wrong!",true,10,code)&&code==409);
  assert(!send("custom.html",4,"wrong!!",true,11,code)&&code==409);
  assert(send("/custom.html",4,"x</h1>",true,10,code)&&code==200);
  assertContent("/custom.html","<h1>x</h1>");
  assert(dashboardTemplateComplete()&&dashboardTemplateOffset()==10&&!strcmp(dashboardTemplatePath(),"/custom.html"));
  assert(!send("../settings.json",0,"x",true,1,code)&&code==400);
  assert(!send("settings.json",0,"x",true,1,code)&&code==400);
  assert(!send("a.b.html",0,"x",true,1,code)&&code==400);
  assert(!send("/.html",0,"x",true,1,code)&&code==400);
  assert(!send("/nested/a.html",0,"x",true,1,code)&&code==400);
  assert(!send("abcdefghijklmnopqrstuv.html",0,"x",true,1,code)&&code==400);
  char large[258]; memset(large,'x',257); large[257]=0;
  assert(!send("custom.html",0,large,true,257,code)&&code==400);
  assert(!send("custom.html",0,"x",true,8193,code)&&code==400);
  assert(!send("custom.html",0,"x",true,2,code)&&code==400);
  assert(!send("custom.html",0,"\xc0\xaf",true,2,code)&&code==400);
  idle=false; assert(!send("custom.html",0,"x",true,1,code)&&code==409); idle=true;
  SPIFFS.used=SPIFFS.total-64*1024; assert(!send("custom.html",0,"x",true,1,code)&&code==507); SPIFFS.used=0;
  existing("preserved"); writeBudget=2;
  assert(!send("custom.html",0,"changed",true,7,code)&&code==507); writeBudget=-1;
  assertContent("/custom.html","preserved");
  renameFailure=1; assert(!send("custom.html",0,"changed",true,7,code)&&code==500);
  assertContent("/custom.html","preserved");
  assert(send("custom.html",0,"one",false,6,code)); writeBudget=1;
  assert(!send("custom.html",3,"two",true,6,code)&&code==507); writeBudget=-1;
  assertContent("/custom.html","preserved");
  assert(SPIFFS.rename("/custom.html","/custom.html.bak"));
  assert(send("custom.html",0,"one",false,6,code)); assertContent("/custom.html","preserved");
  nowMs=UINT32_MAX-30000u; assert(send("custom.html",0,"one",false,6,code));
  nowMs+=59999u; assert(send("custom.html",3,"two",true,6,code)); assertContent("/custom.html","onetwo");
  nowMs=UINT32_MAX-30000u; assert(send("custom.html",0,"one",false,6,code));
  nowMs+=60000u; assert(!send("custom.html",3,"two",true,6,code)&&code==409);
  assertContent("/custom.html","onetwo");
  assert(!SPIFFS.exists(kPrivateTemplate));
  char chunk[257]; memset(chunk,'z',256); chunk[256]=0;
  for(unsigned offset=0;offset<8192;offset+=256) assert(send("custom.html",offset,chunk,offset+256==8192,8192,code));
  File maximum=SPIFFS.open("/custom.html",FILE_READ); assert(maximum&&maximum.size()==8192); maximum.close();
  assert(send("custom.html",0,"\xe2\x98\x83",true,3,code)); assertContent("/custom.html","\xe2\x98\x83");
  assert(send("custom.html",0,"",true,0,code)); assertContent("/custom.html","");
  StaticJsonDocument<384> invalid;
  invalid["path"]="custom.html"; invalid["offset"]=0; invalid["data"]="x"; invalid["final"]=true; invalid["total"]=1; invalid["extra"]=1;
  char message[160]; assert(!dashboardWriteTemplate(invalid.as<JsonObjectConst>(),code,message,sizeof(message))&&code==400);
  invalid.remove("extra"); invalid["offset"]=-1;
  assert(!dashboardWriteTemplate(invalid.as<JsonObjectConst>(),code,message,sizeof(message))&&code==400);
  invalid["offset"]=0; invalid["final"]="yes";
  assert(!dashboardWriteTemplate(invalid.as<JsonObjectConst>(),code,message,sizeof(message))&&code==400);
  puts("PASS: template chunk ordering, atomic finalization, reserve, expiry wrap, UTF-8, and strict validation");
}
