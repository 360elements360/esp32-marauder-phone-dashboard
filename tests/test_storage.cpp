#define DASHBOARD_STORAGE_NATIVE_TEST
#define MARAUDER_DASHBOARD
#include "storage_platform.h"
#include "../firmware/esp32_marauder/DashboardStorage.cpp"
#include <assert.h>
LinkedList<ssid>* ssids=nullptr;
LinkedList<AccessPoint>* access_points=nullptr;
LinkedList<Station>* stations=nullptr;
static void textFile(const char* p,const char* text) {
  File f=SPIFFS.open(p,FILE_WRITE); assert(f); assert(f.write(reinterpret_cast<const uint8_t*>(text),strlen(text))==strlen(text)); f.close();
}
static void recordFile(bool names,const char* const* records,int count,const char* extra=nullptr) {
  File f=SPIFFS.open(filename(names),FILE_WRITE); assert(f);
  assert(writeLine(f,names?"MTD1 SSID":"MTD1 AP")); uint32_t hash=2166136261u;
  for(int i=0;i<count;++i) { assert(writeLine(f,records[i])); hash=checksum(hash,records[i]); }
  char footer[80]; snprintf(footer,sizeof(footer),"E %d %lu",count,static_cast<unsigned long>(hash));
  assert(writeLine(f,footer)); if(extra) assert(writeLine(f,extra)); f.close();
}
static void reset() {
  delete ssids; freeAps(access_points); delete stations;
  ssids=new LinkedList<ssid>(); access_points=new LinkedList<AccessPoint>(); stations=new LinkedList<Station>();
  idle=true; writeBudget=renameFailure=-1; SPIFFS.total=1024*1024; SPIFFS.used=0;
  marauder::RuntimeMemoryGuard::instance().remaining=-1;
  for(int n=0;n<2;++n) { bool names=n!=0; SPIFFS.remove(filename(names)); SPIFFS.remove(temporary(names)); SPIFFS.remove(backup(names)); }
}
static void seed() {
  ssid s; s.essid="test\nSSID"; s.channel=14; memset(s.bssid,0x22,6); ssids->add(s);
  AccessPoint a; a.essid="AP one"; a.channel=6; a.rssi=-72; a.sec=3; a.packets=123; a.wps=true; memset(a.bssid,0x33,6);
  a.stations=new LinkedList<uint16_t>(); a.stations->add(0); access_points->add(a);
  Station station; memset(station.mac,0x44,6); station.ap=0; station.packets=987; stations->add(station);
  Station standalone; memset(standalone.mac,0x55,6); standalone.ap=UINT16_MAX; stations->add(standalone);
}
int main() {
  reset(); seed(); char error[160];
  assert(dashboardSaveTargetList(true,error,sizeof(error))&&dashboardStorageSucceeded()&&!error[0]);
  assert(dashboardSaveTargetList(false,error,sizeof(error)));
  assert(dashboardLoadTargetList(false,error,sizeof(error)));
  assert(access_points->size()==1&&stations->size()==2);
  assert((*access_points)[0].rssi==-72&&(*access_points)[0].sec==3&&(*access_points)[0].packets==123&&(*access_points)[0].wps);
  assert(!strcmp((*access_points)[0].essid.c_str(),"AP one"));
  assert((*access_points)[0].stations->size()==1&&(*(*access_points)[0].stations)[0]==0);
  assert((*stations)[0].ap==0&&(*stations)[0].mac[0]==0x44&&(*stations)[1].ap==UINT16_MAX);
  ssids->clear(); assert(dashboardLoadTargetList(true,error,sizeof(error)));
  assert(ssids->size()==1&&!strcmp((*ssids)[0].essid.c_str(),"test\nSSID")&&(*ssids)[0].bssid[0]==0x22&&(*ssids)[0].channel==14);
  auto* oldNames=ssids; auto* oldAps=access_points; auto* oldStations=stations;
  idle=false; assert(!dashboardLoadTargetList(true,error,sizeof(error))&&!dashboardStorageSucceeded());
  assert(!dashboardSaveTargetList(false,error,sizeof(error))); idle=true;
  (*ssids)[0].essid="unsaved change";
  writeBudget=20; assert(!dashboardSaveTargetList(true,error,sizeof(error))); writeBudget=-1;
  assert(dashboardLoadTargetList(true,error,sizeof(error))); oldNames=ssids;
  assert(!strcmp((*ssids)[0].essid.c_str(),"test\nSSID"));
  (*ssids)[0].essid="another unsaved change";
  renameFailure=1; assert(!dashboardSaveTargetList(true,error,sizeof(error))); assert(SPIFFS.exists(filename(true)));
  assert(dashboardLoadTargetList(true,error,sizeof(error))); oldNames=ssids;
  assert(!strcmp((*ssids)[0].essid.c_str(),"test\nSSID"));
  SPIFFS.used=SPIFFS.total-64*1024; assert(!dashboardSaveTargetList(true,error,sizeof(error))); SPIFFS.used=0;
  marauder::RuntimeMemoryGuard::instance().remaining=0;
  assert(!dashboardLoadTargetList(false,error,sizeof(error))&&access_points==oldAps&&stations==oldStations);
  marauder::RuntimeMemoryGuard::instance().remaining=3;
  assert(!dashboardLoadTargetList(false,error,sizeof(error))&&access_points==oldAps&&stations==oldStations);
  marauder::RuntimeMemoryGuard::instance().remaining=-1;
  for(int budget=0;budget<8;++budget) {
    int before=liveAllocations; nothrowBudget=budget;
    bool loaded=dashboardLoadTargetList(false,error,sizeof(error)); nothrowBudget=-1;
    if(!loaded) { assert(access_points==oldAps&&stations==oldStations); assert(liveAllocations==before); }
    else { oldAps=access_points; oldStations=stations; }
  }
  for(int budget=0;budget<2;++budget) {
    int before=liveAllocations; stringBudget=budget;
    assert(!dashboardLoadTargetList(false,error,sizeof(error))); stringBudget=-1;
    assert(access_points==oldAps&&stations==oldStations&&liveAllocations==before);
  }
  textFile(filename(true),"MTD1 SSID\nS aa:bb:cc:dd:ee:ff 1 41\n");
  assert(!dashboardLoadTargetList(true,error,sizeof(error))&&ssids==oldNames);
  textFile(filename(false),"MTD1 AP\nA aa:bb:cc:dd:ee:ff 1 -1 0 0 0 41\nE 1 0\n");
  assert(!dashboardLoadTargetList(false,error,sizeof(error))&&access_points==oldAps&&stations==oldStations);
  const char* duplicateAp[]={"A aa:bb:cc:dd:ee:ff 1 -1 0 0 0 41","A aa:bb:cc:dd:ee:ff 1 -1 0 0 0 42"};
  const char* missingParent[]={"T aa:bb:cc:dd:ee:ff 01:02:03:04:05:06 0"};
  const char* duplicateStation[]={"T - 01:02:03:04:05:06 0","T - 01:02:03:04:05:06 1"};
  const char* wrongKind[]={"S aa:bb:cc:dd:ee:ff 1 41"};
  const char* const* malformed[]={duplicateAp,missingParent,duplicateStation,wrongKind};
  const int counts[]={2,1,2,1};
  for(int i=0;i<4;++i) {
    recordFile(false,malformed[i],counts[i]); int before=liveAllocations;
    assert(!dashboardLoadTargetList(false,error,sizeof(error)));
    assert(access_points==oldAps&&stations==oldStations&&liveAllocations==before);
  }
  recordFile(false,nullptr,0,"trailing");
  assert(!dashboardLoadTargetList(false,error,sizeof(error))&&access_points==oldAps);
  const char* excessive[513]; for(int i=0;i<513;++i) excessive[i]="S aa:bb:cc:dd:ee:ff 1 41";
  recordFile(true,excessive,513); int before=liveAllocations;
  assert(!dashboardLoadTargetList(true,error,sizeof(error))&&ssids==oldNames&&liveAllocations==before);
  assert(dashboardSaveTargetList(false,error,sizeof(error)));
  assert(SPIFFS.rename(filename(false),backup(false)));
  assert(dashboardLoadTargetList(false,error,sizeof(error))&&SPIFFS.exists(filename(false)));
  reset();
  assert(dashboardSaveTargetList(false,error,sizeof(error))&&dashboardLoadTargetList(false,error,sizeof(error)));
  assert(access_points->size()==0&&stations->size()==0);
  reset(); delete ssids; delete access_points; delete stations; ssids=nullptr; access_points=nullptr; stations=nullptr;
  assert(liveAllocations==0);
  puts("PASS: actual storage save/load, MAC associations, transactional failure preservation, memory guard, backup recovery, empty lists");
}
