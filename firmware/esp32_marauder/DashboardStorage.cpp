#ifndef DASHBOARD_STORAGE_NATIVE_TEST
#include "configs.h"
#endif
#ifdef MARAUDER_DASHBOARD
#include "DashboardStorage.h"
#include "DashboardStorageFormat.h"
#ifndef DASHBOARD_STORAGE_NATIVE_TEST
#include "DashboardBridge.h"
#include "WiFiScan.h"
#include <SPIFFS.h>
#include <new>
#endif
extern LinkedList<ssid>* ssids;
extern LinkedList<AccessPoint>* access_points;
extern LinkedList<Station>* stations;
namespace {
using namespace dashboard_storage;
constexpr size_t kSettingsReserve = 64 * 1024;
bool succeeded = false;
// Upstream LinkedList::add dereferences unchecked throwing-new allocations.
// These staged containers remain compatible with normal firmware list callers,
// but allocate their nodes with nothrow and reserve space for String copies.
template <typename T> class StorageList : public LinkedList<T> {
 public:
  bool append(const T& value) {
    if (!marauder::RuntimeMemoryGuard::instance().allow(sizeof(ListNode<T>) + 512)) return false;
    ListNode<T>* node = new (std::nothrow) ListNode<T>();
    if (!node) return false;
    node->data = value; node->next = nullptr;
    if (this->root) this->last->next = node; else this->root = node;
    this->last = node; ++this->_size; this->isCached = false;
    return true;
  }
  bool add(T value) override { return append(value); }
};
template <typename T> StorageList<T>* makeList() {
  return marauder::RuntimeMemoryGuard::instance().allow(sizeof(StorageList<T>) + 512)
    ? new (std::nothrow) StorageList<T>() : nullptr;
}
void freeAps(LinkedList<AccessPoint>* list) {
  if (!list) return;
  for (int i=0; i<list->size(); ++i) delete (*list)[i].stations;
  delete list;
}
bool result(bool ok, const char* message, char* error, size_t capacity) {
  succeeded = ok;
  if (error && capacity) snprintf(error, capacity, "%s", ok ? "" : message);
  Serial.printf("Target storage: %s\n", message);
  return ok;
}
const char* filename(bool ssidList) { return ssidList ? "/SSIDs_0.log" : "/APs_0.log"; }
const char* temporary(bool ssidList) { return ssidList ? "/SSIDs_0.tmp" : "/APs_0.tmp"; }
const char* backup(bool ssidList) { return ssidList ? "/SSIDs_0.bak" : "/APs_0.bak"; }
bool availableStorage() { return SPIFFS.totalBytes() > 0; }
bool recover(bool ssidList) {
  // A reset during the two rename operations leaves the old validated file here.
  return SPIFFS.exists(filename(ssidList)) || !SPIFFS.exists(backup(ssidList))
    || SPIFFS.rename(backup(ssidList), filename(ssidList));
}
bool writeLine(File& file, const char* text) {
  const size_t total=SPIFFS.totalBytes(),used=SPIFFS.usedBytes(),bytes=strlen(text)+1;
  if (used>total || total-used < kSettingsReserve+bytes+4096 || file.size()+bytes>kMaxFileBytes) return false;
  return file.write(reinterpret_cast<const uint8_t*>(text),bytes-1)==bytes-1 && file.write(static_cast<uint8_t>('\n'))==1;
}
// No readStringUntil: an attacker-controlled file cannot grow a String or nest JSON.
bool readLine(File& file, char* line) {
  size_t length=0;
  while (file.available()) {
    int c=file.read();
    if(c=='\n') { line[length]=0; return length>0; }
    if(c<=0 || length+1>=kLineCapacity) return false;
    line[length++]=static_cast<char>(c);
  }
  return false; // Every record, including the integrity footer, must be terminated.
}
bool setName(String& name,const Record& r) {
  if(!r.nameLength) { name=""; return true; }
  return name.reserve(r.nameLength) && name.concat(r.name,r.nameLength) && name.length()==r.nameLength;
}
int findAp(LinkedList<AccessPoint>* aps,const uint8_t* mac) {
  for(int i=0; i<aps->size(); ++i) if(!memcmp((*aps)[i].bssid,mac,6)) return i;
  return -1;
}
struct Staged {
  StorageList<ssid>* names=nullptr;
  StorageList<AccessPoint>* aps=nullptr;
  StorageList<Station>* clients=nullptr;
  ~Staged() { delete names; freeAps(aps); delete clients; }
};
bool stageRecord(Staged& stage, const Record& record, bool ssidList) {
  if(ssidList) {
    if(record.type!='S'||stage.names->size()>=static_cast<int>(kMaxSsids)) return false;
    ssid entry{}; entry.channel=record.channel; memcpy(entry.bssid,record.mac,6);
    if(!setName(entry.essid,record)||!stage.names->append(entry)) return false;
    return (*stage.names)[stage.names->size()-1].essid.length()==record.nameLength;
  }
  if(record.type=='A') {
    // APs precede station records, so every association resolves immediately.
    if(stage.clients->size()||stage.aps->size()>=static_cast<int>(kMaxAps)||findAp(stage.aps,record.mac)>=0) return false;
    AccessPoint entry{};
    entry.channel=record.channel; entry.rssi=record.rssi; entry.sec=record.security;
    entry.packets=record.packets; entry.wps=record.wps; memcpy(entry.bssid,record.mac,6);
    if(!setName(entry.essid,record)) return false;
    entry.stations=makeList<uint16_t>(); if(!entry.stations) return false;
    if(!stage.aps->append(entry)) { delete entry.stations; return false; }
    return (*stage.aps)[stage.aps->size()-1].essid.length()==record.nameLength;
  }
  if(record.type!='T'||stage.clients->size()>=static_cast<int>(kMaxStations)) return false;
  for(int i=0; i<stage.clients->size(); ++i) if(!memcmp((*stage.clients)[i].mac,record.mac,6)) return false;
  int parent=record.associated?findAp(stage.aps,record.parent):-1;
  if(record.associated&&parent<0) return false;
  Station entry{}; memcpy(entry.mac,record.mac,6); entry.packets=record.packets;
  entry.ap=parent<0?UINT16_MAX:static_cast<uint16_t>(parent);
  if(!stage.clients->append(entry)) return false;
  if(parent<0) return true;
  auto* links=static_cast<StorageList<uint16_t>*>((*stage.aps)[parent].stations);
  return links->append(static_cast<uint16_t>(stage.clients->size()-1));
}
bool loadFile(File& file, bool ssidList, Staged& stage) {
  if(file.size()>kMaxFileBytes) return false;
  char line[kLineCapacity];
  if(!readLine(file,line)||strcmp(line,ssidList?"MTD1 SSID":"MTD1 AP")) return false;
  if(ssidList) { stage.names=makeList<ssid>(); if(!stage.names) return false; }
  else { stage.aps=makeList<AccessPoint>(); stage.clients=makeList<Station>(); if(!stage.aps||!stage.clients) return false; }
  uint32_t count=0, hash=2166136261u;
  while(file.available()) {
    if(!readLine(file,line)) return false;
    if(line[0]=='E') return parseFooter(line,count,hash)&&!file.available();
    Record record;
    if(!parseRecord(line,record)||!stageRecord(stage,record,ssidList)) return false;
    hash=checksum(hash,line); ++count;
    yield();
  }
  return false;
}
bool writeRecord(File& file, const Record& record,uint32_t& count,uint32_t& hash) {
  char line[kLineCapacity];
  if(!formatRecord(record,line,sizeof(line))||!writeLine(file,line)) return false;
  ++count; hash=checksum(hash,line); yield(); return true;
}
bool copyName(const String& name,Record& record) {
  record.nameLength=name.length(); if(record.nameLength>32) return false;
  memcpy(record.name,name.c_str(),record.nameLength); return true;
}
bool saveFile(File& file,bool ssidList) {
  if(!writeLine(file,ssidList?"MTD1 SSID":"MTD1 AP")) return false;
  uint32_t count=0,hash=2166136261u;
  if(ssidList) {
    if(!ssids||ssids->size()>static_cast<int>(kMaxSsids)) return false;
    for(int i=0; i<ssids->size(); ++i) {
      const ssid& value=(*ssids)[i]; Record record; record.type='S'; record.channel=value.channel;
      memcpy(record.mac,value.bssid,6);
      if(!copyName(value.essid,record)||!writeRecord(file,record,count,hash)) return false;
    }
  } else {
    if(!access_points||!stations||access_points->size()>static_cast<int>(kMaxAps)||stations->size()>static_cast<int>(kMaxStations)) return false;
    // Store associations as MACs, never transient list indexes. A station with no
    // retained AP remains unassociated; conflicting links are rejected.
    uint16_t* parents=nullptr;
    if(stations->size()) {
      size_t bytes=sizeof(uint16_t)*stations->size();
      if(!marauder::RuntimeMemoryGuard::instance().allow(bytes+512)) return false;
      parents=new(std::nothrow) uint16_t[stations->size()]; if(!parents) return false;
      for(int i=0; i<stations->size(); ++i) parents[i]=UINT16_MAX;
    }
    bool valid=true;
    for(int i=0; valid&&i<access_points->size(); ++i) {
      const AccessPoint& value=(*access_points)[i]; Record record; record.type='A';
      record.channel=value.channel; record.rssi=value.rssi; record.security=value.sec;
      record.packets=value.packets; record.wps=value.wps; memcpy(record.mac,value.bssid,6);
      if(findAp(access_points,value.bssid)!=i||!copyName(value.essid,record)||!writeRecord(file,record,count,hash)) { valid=false; break; }
      if(!value.stations) { valid=false; break; }
      for(int j=0; valid&&j<value.stations->size(); ++j) {
        uint16_t index=(*value.stations)[j];
        if(index>=stations->size()||parents[index]!=UINT16_MAX) { valid=false; break; }
        parents[index]=static_cast<uint16_t>(i);
      }
    }
    for(int i=0; valid&&i<stations->size(); ++i) {
      const Station& value=(*stations)[i]; Record record; record.type='T'; record.packets=value.packets;
      memcpy(record.mac,value.mac,6); record.associated=parents[i]!=UINT16_MAX;
      if(record.associated) memcpy(record.parent,(*access_points)[parents[i]].bssid,6);
      for(int j=0; j<i; ++j) if(!memcmp((*stations)[j].mac,value.mac,6)) valid=false;
      if(valid&&!writeRecord(file,record,count,hash)) valid=false;
    }
    delete[] parents; if(!valid) return false;
  }
  char footer[kLineCapacity]; snprintf(footer,sizeof(footer),"E %lu %lu",static_cast<unsigned long>(count),static_cast<unsigned long>(hash));
  return writeLine(file,footer);
}
}
bool dashboardStorageSucceeded() { return succeeded; }
bool dashboardSaveTargetList(bool ssidList,char* error,size_t capacity) {
  succeeded=false;
  if(!dashboardTargetsIdle()) return result(false,"Stop active radio tasks before saving lists",error,capacity);
  if(!availableStorage()||!recover(ssidList)) return result(false,"Internal storage unavailable",error,capacity);
  SPIFFS.remove(temporary(ssidList));
  File file=SPIFFS.open(temporary(ssidList),FILE_WRITE);
  bool valid=file&&saveFile(file,ssidList);
  if(file) { file.flush(); file.close(); }
  if(!valid) { SPIFFS.remove(temporary(ssidList)); return result(false,"Save failed: invalid targets, memory pressure, or storage reserve reached",error,capacity); }
  // Remove only fixed internal paths. The prior file survives a failed install.
  if(SPIFFS.exists(backup(ssidList))&&!SPIFFS.remove(backup(ssidList))) {
    SPIFFS.remove(temporary(ssidList)); return result(false,"Could not prepare saved-list backup",error,capacity);
  }
  bool previous=SPIFFS.exists(filename(ssidList));
  if(previous&&!SPIFFS.rename(filename(ssidList),backup(ssidList))) {
    SPIFFS.remove(temporary(ssidList)); return result(false,"Could not back up saved list",error,capacity);
  }
  if(!SPIFFS.rename(temporary(ssidList),filename(ssidList))) {
    if(previous) SPIFFS.rename(backup(ssidList),filename(ssidList));
    SPIFFS.remove(temporary(ssidList)); return result(false,"Could not install saved list; previous list retained in backup",error,capacity);
  }
  SPIFFS.remove(backup(ssidList));
  return result(true,ssidList?"Saved SSIDs to /SSIDs_0.log":"Saved APs and stations to /APs_0.log",error,capacity);
}
bool dashboardLoadTargetList(bool ssidList,char* error,size_t capacity) {
  succeeded=false;
  if(!dashboardTargetsIdle()) return result(false,"Stop active radio tasks before loading lists",error,capacity);
  if(!availableStorage()||!recover(ssidList)) return result(false,"Internal storage unavailable",error,capacity);
  File file=SPIFFS.open(filename(ssidList),FILE_READ);
  if(!file) return result(false,"Saved target list not found",error,capacity);
  Staged stage; bool valid=loadFile(file,ssidList,stage); file.close();
  if(!valid) return result(false,"Load rejected: malformed or truncated list, limits exceeded, or memory pressure; current targets preserved",error,capacity);
  // Commit is pointer replacement after complete validation, while callbacks are off.
  if(ssidList) { auto* old=ssids; ssids=stage.names; stage.names=nullptr; delete old; }
  else {
    auto* oldAps=access_points; auto* oldStations=stations;
    access_points=stage.aps; stations=stage.clients; stage.aps=nullptr; stage.clients=nullptr;
    freeAps(oldAps); delete oldStations;
  }
  return result(true,ssidList?"Loaded saved SSIDs":"Loaded saved APs and station associations",error,capacity);
}
#endif
