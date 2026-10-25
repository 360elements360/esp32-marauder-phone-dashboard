#ifdef MARAUDER_DASHBOARD
#include "ToolBridge.h"
#include "ToolCatalog.h"
#include <math.h>
#include <memory>
#include <new>
namespace {
int toolError(char* error,size_t capacity,int code,const char* text) { workflowCopy(error,capacity,text);return code; }
}
int ToolRunner::prepare(JsonObjectConst request,char* error,size_t capacity,bool execution) {
  const char* id=request["tool"] | "";
  definition_=nullptr;
  for(size_t i=0;i<TOOL_CATALOG_COUNT;++i) if(!strcmp(TOOL_CATALOG[i].id,id)){definition_=TOOL_CATALOG[i].json;break;}
  if(!definition_)return toolError(error,capacity,404,"Unknown tool");
  DynamicJsonDocument row(8192);
  if(deserializeJson(row,definition_) || !row.is<JsonObject>())return toolError(error,capacity,500,"Invalid device tool definition");
  if(!row["available"].as<bool>())return toolError(error,capacity,503,row["reason"] | "Tool unavailable on this hardware");
  if(!strcmp(row["requires"]|"", "joined") && !dashboardToolCheck("joined"))return toolError(error,capacity,409,"Join a Wi-Fi network before using this tool");
  workflowCopy(during_,sizeof(during_),row["during"]|"");stop_=row["stop"]|false;
  for(JsonPairConst pair:request) if(strcmp(pair.key().c_str(),"action") && strcmp(pair.key().c_str(),"kind") && strcmp(pair.key().c_str(),"tool") && strcmp(pair.key().c_str(),"inputs") && strcmp(pair.key().c_str(),"seconds") && strcmp(pair.key().c_str(),"channel"))return toolError(error,capacity,400,"Unknown task property");
  if(!request["inputs"].is<JsonObjectConst>() || !request["seconds"].is<int>() || !request["channel"].is<int>())return toolError(error,capacity,400,"Expected inputs object, whole seconds and channel");
  int seconds=request["seconds"],channel=request["channel"];
  if(seconds<1 || seconds>300 || channel<0 || channel>14)return toolError(error,capacity,400,"Use 1 to 300 seconds and channel 0 to 14");
  JsonObjectConst inputs=request["inputs"];JsonArrayConst fields=row["fields"].as<JsonArrayConst>();
  if(fields.size()>12)return toolError(error,capacity,500,"Too many tool fields");
  for(JsonPairConst pair:inputs) { bool found=false;for(JsonObjectConst field:fields)if(!strcmp(field["name"]|"",pair.key().c_str()))found=true;if(!found)return toolError(error,capacity,400,"Unknown tool input"); }
  // Only the requested row's actual fields need scratch space. Keep the
  // bounded workspace off the 8 KiB Arduino loop-task stack.
  std::unique_ptr<ToolArgument[]> args(fields.size() ? new (std::nothrow) ToolArgument[fields.size()] : nullptr);
  if (fields.size() && !args) return toolError(error,capacity,503,"Insufficient memory for tool input validation");
  size_t count=0;
  for(JsonObjectConst field:fields) {
    ToolArgument& arg=args[count++];const char* name=field["name"]|"",*type=field["type"]|"text";
    if(!*name || strlen(name)>=sizeof(arg.name))return toolError(error,capacity,500,"Invalid tool field name");
    workflowCopy(arg.name,sizeof(arg.name),name);
    JsonVariantConst value=inputs.containsKey(name)?inputs[name]:field["default"];
    if(value.isNull()) { if(field["required"]|false)return toolError(error,capacity,400,"A required input is missing");workflowCopy(arg.value,sizeof(arg.value),"");continue; }
    if(!strcmp(type,"integer") || !strcmp(type,"number")) {
      if(value.is<bool>() || (!strcmp(type,"integer") ? !value.is<int>() : !(value.is<double>()||value.is<int>())))return toolError(error,capacity,400,"A numeric input has the wrong type");
      double n=value.as<double>();if(!isfinite(n) || (!field["min"].isNull()&&n<field["min"].as<double>()) || (!field["max"].isNull()&&n>field["max"].as<double>()))return toolError(error,capacity,400,"A numeric input is outside its allowed range");
      if(!strcmp(type,"integer"))snprintf(arg.value,sizeof(arg.value),"%d",value.as<int>());else snprintf(arg.value,sizeof(arg.value),"%.9g",n);
    } else if(!strcmp(type,"boolean")) {
      if(!value.is<bool>())return toolError(error,capacity,400,"Expected a boolean input");workflowCopy(arg.value,sizeof(arg.value),value.as<bool>()?"true":"false");
    } else if(!strcmp(type,"target")) {
      const char* kind=field["kind"]|"";bool multiple=field["multiple"]|false;
      if((multiple && !value.is<JsonArrayConst>()) || (!multiple && !value.is<const char*>()))return toolError(error,capacity,400,"Expected target identities");
      size_t targets=multiple?value.as<JsonArrayConst>().size():1;
      if(!targets || targets>8)return toolError(error,capacity,400,"Choose 1 to 8 target identities");
      arg.target=true;
      for(size_t n=0;n<targets;++n) {
        JsonVariantConst entry=multiple?value[n]:value;if(!entry.is<const char*>())return toolError(error,capacity,400,"Target identity must be text");
        const char* identity=entry.as<const char*>();WorkflowTarget target;int index=-1,ap=-1;
        if(!dashboardToolResolve(kind,identity,target,index,ap))return toolError(error,capacity,404,"Target is stale. Discover targets again.");
        if(!n){workflowCopy(arg.value,sizeof(arg.value),target.id);arg.apIndex=ap;}
        char indexText[16];snprintf(indexText,sizeof(indexText),"%s%d",n?",":"",index);
        if(strlen(arg.indices)+strlen(indexText)>=sizeof(arg.indices))return toolError(error,capacity,400,"Target list too long");strcat(arg.indices,indexText);
      }
    } else {
      if(!value.is<const char*>())return toolError(error,capacity,400,"Expected a text input");
      const char* text=value.as<const char*>();size_t maximum=field["maxLength"]|128;
      if(maximum>128)maximum=128;
      if(!toolSafeText(text,maximum) || ((field["required"]|false)&&!*text))return toolError(error,capacity,400,"Text is empty, too long, contains a quote or control character");
      const char* format=field["format"]|"";uint8_t mac[6];
      if ((!strcmp(format,"mac") && !workflowParseMac(text,mac)) ||
          (!strcmp(format,"public_html") && !toolPublicHtml(text)) ||
          (!strcmp(format,"filter") && (strncmp(text,"equals ",7) || !text[7]) && (strncmp(text,"contains ",9) || !text[9])))
        return toolError(error,capacity,400,"Input does not match the required format");
      if(!strcmp(type,"select")) { bool found=false;for(JsonObjectConst option:field["options"].as<JsonArrayConst>())if(!strcmp(option["value"]|"",text))found=true;if(!found)return toolError(error,capacity,400,"Unknown input choice"); }
      else if(strcmp(type,"text") && strcmp(type,"password"))return toolError(error,capacity,500,"Unknown field type");
      arg.quoted=strcmp(type,"select")!=0;workflowCopy(arg.value,sizeof(arg.value),text);
    }
  }
  stepCount_=0;next_=0;modeCount_=0;
  for(JsonVariantConst mode:row["modes"].as<JsonArrayConst>()) {if(!mode.is<unsigned int>() || mode.as<unsigned int>()>255 || modeCount_>=16)return toolError(error,capacity,500,"Invalid expected radio modes");modes_[modeCount_++]=mode.as<uint8_t>();}
  for(JsonVariantConst step:row["steps"].as<JsonArrayConst>()) {if(!step.is<const char*>() || stepCount_>=8 || !toolRender(step.as<const char*>(),args.get(),count,channel,seconds,steps_[stepCount_],sizeof(steps_[0])))return toolError(error,capacity,500,"Device recipe cannot be rendered safely");++stepCount_;}
  if(!stepCount_)return toolError(error,capacity,503,"This tool has no executable device recipe");
  timed_=row["timed"]|false;channelPolicy_=row["channel"]|false;workflowCopy(check_,sizeof(check_),row["check"]|"");
  workflowCopy(during_,sizeof(during_),row["during"]|"");stop_=row["stop"]|false;
  if(execution && row["select"].is<JsonObjectConst>()) {
    const char* name=row["select"]["field"]|"",*kind=row["select"]["kind"]|"";const ToolArgument* selected=nullptr;
    for(size_t i=0;i<count;++i)if(!strcmp(name,args[i].name)&&args[i].target)selected=&args[i];
    if(!selected || !dashboardToolSelect(kind,selected->indices))return toolError(error,capacity,409,"Could not prepare target selection");
  }
  if(!execution) {if(measureJson(request)>=sizeof(request_))return toolError(error,capacity,413,"Tool request exceeds storage");serializeJson(request,request_,sizeof(request_));}
  return 0;
}
int ToolRunner::revalidate(char* error,size_t capacity) {
  DynamicJsonDocument request(1536);
  if (!request.capacity()) return toolError(error,capacity,503,"Insufficient memory to validate saved tool inputs");
  if(deserializeJson(request,static_cast<const char*>(request_)))return toolError(error,capacity,500,"Stored tool request invalid");
  return prepare(request.as<JsonObjectConst>(),error,capacity,true);
}
#endif
