#include "configs.h"
#ifdef MARAUDER_DASHBOARD
#include "CommandLine.h"
#include "ToolBridge.h"
#include "DashboardBridge.h"
#include "DashboardStorage.h"
#include <SPIFFS.h>
#include <WiFi.h>
namespace {
struct SavedSelection { bool values[1024] = {}, parents[1024] = {}; size_t count = 0, parentCount = 0; char kind[12] = {}; } savedSelection;
}
bool dashboardToolTargetKind(const char* k) {
  const char* kinds[] = {"wifi","ble","station","airtag","flipper","pineapple","multissid","probe","ip","ssid","remoteid"};
  for(const char* kind:kinds) if(!strcmp(k,kind)) return true;
  return false;
}
size_t dashboardToolTargetCount(const char* k) {
  if(!dashboardTargetsIdle()) return 0;
  if(!strcmp(k,"wifi"))return access_points->size();
  if(!strcmp(k,"ble"))return ble_devices->size();
  if(!strcmp(k,"station"))return stations->size();
  if(!strcmp(k,"airtag"))return airtags->size();
  if(!strcmp(k,"flipper"))return flippers->size();
  if(!strcmp(k,"probe"))return probe_req_ssids->size();
  if(!strcmp(k,"ssid"))return ssids->size();
  if(!strcmp(k,"ip"))return ipList->size();
  if(!strcmp(k,"pineapple"))return wifi_scan_obj.confirmed_pinescan ? wifi_scan_obj.confirmed_pinescan->size() : 0;
  if(!strcmp(k,"multissid"))return wifi_scan_obj.confirmed_multissid ? wifi_scan_obj.confirmed_multissid->size() : 0;
  if(!strcmp(k,"remoteid"))return wifi_scan_obj.remote_id_store.size();
  return 0;
}
bool dashboardToolTargetAt(const char* k, size_t index, WorkflowTarget& target) {
  if(!dashboardTargetsIdle() || index >= dashboardToolTargetCount(k)) return false;
  target=WorkflowTarget();
  if(!strcmp(k,"wifi") || !strcmp(k,"ble")) return dashboardTargetAt(!strcmp(k,"ble"),index,target);
  if(!strcmp(k,"station")) {
    const Station d=stations->get(index); workflowFormatMac(d.mac,target.id);
    workflowCopy(target.name,sizeof(target.name),target.id); target.packets=d.packets; target.lastSeen=d.last_seen_ms;
    if(d.ap < access_points->size()) target.channel=access_points->get(d.ap).channel;
  } else if(!strcmp(k,"airtag")) {
    const AirTag d=airtags->get(index); uint8_t mac[6]; if(!workflowParseMac(d.mac.c_str(),mac))return false;
    workflowFormatMac(mac,target.id); workflowCopy(target.name,sizeof(target.name),d.mac.c_str()); target.rssi=d.rssi; target.lastSeen=d.last_seen;
  } else if(!strcmp(k,"flipper")) {
    const Flipper d=flippers->get(index); uint8_t mac[6]; if(!workflowParseMac(d.mac.c_str(),mac))return false;
    workflowFormatMac(mac,target.id); workflowCopy(target.name,sizeof(target.name),d.name.c_str());target.rssi=d.rssi;target.lastSeen=d.last_seen;
  } else if(!strcmp(k,"probe")) {
    const ProbeReqSsid d=probe_req_ssids->get(index);toolContentIdentity(k,d.essid.c_str(),target.id);workflowCopy(target.name,sizeof(target.name),d.essid.c_str());target.packets=d.requests;
  } else if(!strcmp(k,"ssid")) {
    const ssid d=ssids->get(index); String content=d.essid+"|"+String(d.channel)+"|"+macToString(d.bssid);
    toolContentIdentity(k,content.c_str(),target.id);workflowCopy(target.name,sizeof(target.name),d.essid.c_str());target.channel=d.channel;
  } else if(!strcmp(k,"ip")) {
    String ip=ipList->get(index).toString();workflowCopy(target.id,sizeof(target.id),ip.c_str());workflowCopy(target.name,sizeof(target.name),ip.c_str());
  } else if(!strcmp(k,"pineapple")) {
    const auto d=wifi_scan_obj.confirmed_pinescan->get(index);workflowFormatMac(d.mac,target.id);workflowCopy(target.name,sizeof(target.name),d.essid.c_str());target.rssi=d.rssi;target.channel=d.channel;
  } else if(!strcmp(k,"multissid")) {
    const auto d=wifi_scan_obj.confirmed_multissid->get(index);workflowFormatMac(d.mac,target.id);workflowCopy(target.name,sizeof(target.name),d.essid.c_str());target.rssi=d.rssi;target.channel=d.channel;
  } else if(!strcmp(k,"remoteid")) {
    const auto* d=wifi_scan_obj.remote_id_store.at(index);if(!d)return false;
    workflowFormatMac(d->mac,target.id);workflowCopy(target.name,sizeof(target.name),d->hasUasId?d->uasId:target.id);target.rssi=d->rssi;target.channel=d->lastWifiChannel;target.packets=d->packetCount;target.lastSeen=d->lastSeenMs;
  } else return false;
  return true;
}
bool dashboardToolResolve(const char* k,const char* id,WorkflowTarget& target,int& index,int& apIndex) {
  if(!dashboardToolTargetKind(k) || !dashboardTargetsIdle())return false;
  for(size_t n=0,count=dashboardToolTargetCount(k);n<count;++n) {
    if(!dashboardToolTargetAt(k,n,target))continue;
    bool match=!strcmp(id,target.id); uint8_t mac[6]; if(workflowParseMac(id,mac))match=workflowTargetMatches(target,mac);
    if(match) { index=n;apIndex=-1;
      if(!strcmp(k,"station")) { const Station d=stations->get(n); if(d.ap>=access_points->size())return false;apIndex=d.ap;
        const auto ap=access_points->get(d.ap);bool belongs=false;if(ap.stations)for(int j=0;j<ap.stations->size();++j)if(ap.stations->get(j)==n)belongs=true;if(!belongs)return false;
      }return true;
    }
  }return false;
}
bool dashboardToolSelect(const char* k,const char* indices) {
  if(!dashboardTargetsIdle() || savedSelection.count || (strcmp(k,"wifi")&&strcmp(k,"station")&&strcmp(k,"ssid")))return false;
  const size_t count=dashboardToolTargetCount(k);if(count>1024)return false;
  uint16_t selected[8]={};size_t selectedCount=0;const char* p=indices;
  auto selectedAt = [&](size_t index) {for(size_t n=0;n<selectedCount;++n)if(selected[n]==index)return true;return false;};
  while(*p){char* end=nullptr;unsigned long n=strtoul(p,&end,10);if(end==p||n>=count||selectedCount>=8||(*end&&*end!=','))return false;selected[selectedCount++]=n;p=*end?end+1:end;}
  workflowCopy(savedSelection.kind,sizeof(savedSelection.kind),k);savedSelection.count=count;
  if(!strcmp(k,"station")) {
    if(access_points->size()>1024){savedSelection.count=0;savedSelection.parentCount=0;savedSelection.kind[0]=0;return false;}
    savedSelection.parentCount=access_points->size();
    for(size_t i=0;i<savedSelection.parentCount;++i){auto ap=access_points->get(i);savedSelection.parents[i]=ap.selected;ap.selected=false;access_points->set(i,ap);}
    for(size_t i=0;i<count;++i)if(selectedAt(i)){const auto station=stations->get(i);if(station.ap<access_points->size()){auto ap=access_points->get(station.ap);ap.selected=true;access_points->set(station.ap,ap);}}
  }
  for(size_t i=0;i<count;++i) {
    if(!strcmp(k,"wifi")){auto d=access_points->get(i);savedSelection.values[i]=d.selected;d.selected=selectedAt(i);access_points->set(i,d);}
    else if(!strcmp(k,"station")){auto d=stations->get(i);savedSelection.values[i]=d.selected;d.selected=selectedAt(i);stations->set(i,d);}
    else {auto d=ssids->get(i);savedSelection.values[i]=d.selected;d.selected=selectedAt(i);ssids->set(i,d);}
  }return true;
}
void dashboardToolRestoreSelection() {
  if(!dashboardTargetsIdle())return;
  const char* k=savedSelection.kind;
  if(!strcmp(k,"station") && savedSelection.parentCount==size_t(access_points->size()))
    for(size_t i=0;i<savedSelection.parentCount;++i){auto ap=access_points->get(i);ap.selected=savedSelection.parents[i];access_points->set(i,ap);}
  if(savedSelection.count==dashboardToolTargetCount(k)) for(size_t i=0;i<savedSelection.count;++i) {
    if(!strcmp(k,"wifi")){auto d=access_points->get(i);d.selected=savedSelection.values[i];access_points->set(i,d);}
    else if(!strcmp(k,"station")){auto d=stations->get(i);d.selected=savedSelection.values[i];stations->set(i,d);}
    else if(!strcmp(k,"ssid")){auto d=ssids->get(i);d.selected=savedSelection.values[i];ssids->set(i,d);}
  }savedSelection.count=0;savedSelection.parentCount=0;savedSelection.kind[0]=0;
}
bool dashboardToolCheck(const char* check) {
  if(!*check)return true;
  if(!strcmp(check,"joined"))return WiFi.status()==WL_CONNECTED;
  if(!strcmp(check,"portal"))return dashboardMode()==WIFI_SCAN_EVIL_PORTAL && evil_portal_obj.dashboardRunning();
  if(!strcmp(check,"capture"))return dashboardCaptureHealthy();
  if(!strcmp(check,"recon"))return recon_obj.active();
  if(!strcmp(check,"storage"))return dashboardStorageSucceeded();
  #ifdef HAS_CSI_MOTION
  if(!strcmp(check,"csi"))return csi_motion_obj.active();
  #endif
  return false;
}
#endif
