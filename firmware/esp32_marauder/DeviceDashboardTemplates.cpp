#include "DeviceDashboard.h"
#ifdef MARAUDER_DASHBOARD
#include "DashboardHttpServer.h"
#include "DashboardTemplates.h"
void DeviceDashboard::templateUpload() {
  if(!body_.valid()){reply(413,"Template request exceeds 512 bytes");return;}
  if(workflow_.active() || queue_.size()){reply(409,"Stop the active task before uploading a template");return;}
  if(!toolSafeJson(body_.data())){reply(400,"Null characters are not allowed in templates");return;}
  StaticJsonDocument<1024> request;
  if(deserializeJson(request,body_.data(),body_.size()) || !request.is<JsonObject>()) {reply(400,"Expected a template chunk object");return;}
  char message[161]={};int code=400;
  if(!dashboardWriteTemplate(request.as<JsonObjectConst>(),code,message,sizeof(message))) {reply(code,message);return;}
  StaticJsonDocument<256> response;response["path"]=dashboardTemplatePath();response["offset"]=dashboardTemplateOffset();response["complete"]=dashboardTemplateComplete();
  String json;serializeJson(response,json);http_.send(200,"application/json",json);
}
#endif
