#include "../firmware/esp32_marauder/ToolBridge.h"
#include "../firmware/esp32_marauder/ToolCatalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>
bool failToolScratch=false;size_t toolScratchBytes=0;
void* operator new[](size_t bytes,const std::nothrow_t&) noexcept {toolScratchBytes=bytes;if(failToolScratch)return nullptr;return ::operator new[](bytes);}
bool joined=false; int targetIndex=4; bool stale=false; bool selection=false;
bool dashboardToolCheck(const char* check){return !strcmp(check,"joined")?joined:true;}
bool dashboardToolResolve(const char* kind,const char* identity,WorkflowTarget& target,int& index,int& apIndex) {
  (void)kind;
  if(stale || strcmp(identity,"aa:bb:cc:dd:ee:ff"))return false;
  index=targetIndex;apIndex=2;workflowCopy(target.id,sizeof(target.id),identity);return true;
}
bool dashboardToolSelect(const char*,const char* indices){selection=!strcmp(indices,"7");return selection;}
int request(ToolRunner& tool,const char* id,const char* inputs="{}",const char* extra="") {
  char text[1024];snprintf(text,sizeof(text),"{\"action\":\"start\",\"kind\":\"tool\",\"tool\":\"%s\",\"inputs\":%s,\"seconds\":30,\"channel\":1%s}",id,inputs,extra);
  StaticJsonDocument<2048> doc;assert(!deserializeJson(doc,text));char error[161];return tool.prepare(doc.as<JsonObjectConst>(),error,sizeof(error));
}
int main(){
  ToolRunner tool;
  failToolScratch=true;assert(request(tool,"device_channel")==503);failToolScratch=false;
  assert(request(tool,"device_channel")==0);assert(toolScratchBytes==sizeof(ToolArgument));
  toolScratchBytes=0;assert(request(tool,"device_info")==0);assert(toolScratchBytes==0);
  assert(request(tool,"device_channel")==0);assert(!strcmp(tool.nextStep(),"channel -s 1"));
  assert(request(tool,"device_channel","{\"radioChannel\":15}")==400);
  assert(request(tool,"device_channel","{\"radioChannel\":true}")==400);
  assert(request(tool,"device_channel","{\"radioChannel\":\"1\"}")==400);
  assert(request(tool,"device_channel","{\"mystery\":1}")==400);
  assert(request(tool,"device_channel","{}",",\"evil\":1")==400);
  assert(request(tool,"not-real")==404);
  assert(request(tool,"sd_backup")==503);
  assert(request(tool,"lan_arp")==409);joined=true;assert(request(tool,"lan_arp")==0);
  assert(request(tool,"ssid_add","{\"name\":\"two words\"}")==0);assert(!strcmp(tool.nextStep(),"ssid -a -n \"two words\""));
  assert(request(tool,"ssid_add","{\"name\":\"bad\\\" -r 2\"}")==400);
  assert(request(tool,"target_add_ap","{\"mac\":\"bad\",\"name\":\"Test\"}")==400);
  assert(request(tool,"portal_start","{\"file\":\"settings.json\"}")==400);
  assert(request(tool,"portal_start","{\"file\":\"../index.html\"}")==400);
  assert(request(tool,"portal_start")==0);assert(!strcmp(tool.nextStep(),"evilportal -c start -w \"index.html\""));
  assert(request(tool,"select_filter","{\"filter\":\"anything\"}")==400);
  assert(request(tool,"select_filter","{\"filter\":\"contains Lab\"}")==0);
  assert(request(tool,"wifi_client_signal","{\"target\":\"aa:bb:cc:dd:ee:ff\"}")==0);
  targetIndex=7;char error[161];assert(tool.revalidate(error,sizeof(error))==0);
  assert(!strcmp(tool.nextStep(),"foxhunt -s 2 7"));
  assert(request(tool,"wifi_client_signal","{\"target\":3}")==400);
  assert(request(tool,"wifi_pmkid_active","{\"target\":[\"aa:bb:cc:dd:ee:ff\"]}")==0);
  assert(tool.revalidate(error,sizeof(error))==0 && selection);
  stale=true;assert(tool.revalidate(error,sizeof(error))==404);
  assert(request(tool,"change_setting","{\"setting\":\"unknown\",\"value\":\"enable\"}")==400);
  assert(request(tool,"network_join","{\"target\":\"aa:bb:cc:dd:ee:ff\"}")==404);
  stale=false;assert(request(tool,"network_join","{\"target\":\"aa:bb:cc:dd:ee:ff\"}")==0);
  assert(!strcmp(tool.nextStep(),"join -a 7 -p \"\""));
  unsigned exercised=0;
  for(size_t i=0;i<TOOL_CATALOG_COUNT;++i) {
    DynamicJsonDocument row(8192);assert(!deserializeJson(row,TOOL_CATALOG[i].json));
    if(!row["available"].as<bool>() || !row["legacy"].isNull() || !row["view"].isNull())continue;
    StaticJsonDocument<2048> job;job["action"]="start";job["kind"]="tool";job["tool"]=TOOL_CATALOG[i].id;job["seconds"]=30;job["channel"]=1;
    JsonObject inputs=job.createNestedObject("inputs");
    for(JsonObjectConst f:row["fields"].as<JsonArrayConst>()) {
      const char* name=f["name"],*type=f["type"]|"text",*format=f["format"]|"";
      if(!f["default"].isNull())inputs[name].set(f["default"]);
      else if(!strcmp(type,"target")) {
        if(f["multiple"]|false)inputs.createNestedArray(name).add("aa:bb:cc:dd:ee:ff");else inputs[name]="aa:bb:cc:dd:ee:ff";
      } else if(!strcmp(type,"integer")||!strcmp(type,"number"))inputs[name]=f["min"]|1;
      else if(!strcmp(type,"select"))inputs[name].set(f["options"][0]["value"]);
      else if(!strcmp(type,"boolean"))inputs[name]=false;
      else inputs[name]=!strcmp(format,"mac")?"aa:bb:cc:dd:ee:ff":!strcmp(format,"filter")?"contains Lab":"Lab";
      if(!strcmp(format,"filter"))inputs[name]="contains Lab";
    }
    char error[161];int code=tool.prepare(job.as<JsonObjectConst>(),error,sizeof(error));
    if(code){printf("catalog row %s returned %d: %s\n",TOOL_CATALOG[i].id,code,error);fflush(stdout);}
    assert(code==0);assert(tool.hasStep());while(tool.hasStep())assert(strlen(tool.nextStep())<=256);++exercised;
  }
  printf("validated %u complete executable flash recipes\n",exercised);
  puts("tool runner: real flash rows, strict types/choices/formats/defaults, stale/reordered identities, selection, unavailable dependencies and quoted recipes passed");
}
