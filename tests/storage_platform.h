#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
namespace std { struct nothrow_t {}; extern const nothrow_t nothrow; }
// Exercise the actual storage implementation and the pinned LinkedList layout.
// File I/O uses local scratch files; radio and memory pressure are deterministic.
static int liveAllocations=0, nothrowBudget=-1, stringBudget=-1;
void* operator new(size_t bytes) { void* p=malloc(bytes); if(!p) abort(); ++liveAllocations; return p; }
void operator delete(void* p) noexcept { if(p) --liveAllocations; free(p); }
void operator delete(void* p,size_t) noexcept { if(p) --liveAllocations; free(p); }
namespace std { const nothrow_t nothrow{}; }
void* operator new(size_t bytes,const std::nothrow_t&) noexcept {
  if(!nothrowBudget) return nullptr; if(nothrowBudget>0) --nothrowBudget;
  void* p=malloc(bytes); if(p) ++liveAllocations; return p;
}
void* operator new[](size_t bytes,const std::nothrow_t& flag) noexcept { return operator new(bytes,flag); }
void operator delete[](void* p) noexcept { if(p) --liveAllocations; free(p); }
class String {
  char* data_=nullptr; size_t length_=0, capacity_=0;
 public:
  String()=default;
  String(const String& s) { *this=s; }
  ~String() { if(data_) --liveAllocations; free(data_); }
  String& operator=(const String& s) { return *this=s.c_str(); }
  String& operator=(const char* s) {
    size_t n=strlen(s); if(!reserve(n)) return *this;
    if(n) memcpy(data_,s,n); length_=n; if(data_) data_[n]=0; return *this;
  }
  const char* c_str() const { return data_?data_:""; }
  size_t length() const { return length_; }
  bool reserve(size_t n) {
    if(n<=capacity_) return true;
    if(!stringBudget) return false; if(stringBudget>0) --stringBudget;
    bool existed=data_!=nullptr; void* p=realloc(data_,n+1); if(!p) return false;
    if(!existed) ++liveAllocations;
    data_=static_cast<char*>(p); capacity_=n; data_[length_]=0; return true;
  }
  bool concat(const char* s,size_t n) {
    if(!reserve(length_+n)) return false;
    memcpy(data_+length_,s,n); length_+=n; data_[length_]=0; return true;
  }
};
#include "../.deps/LinkedList/LinkedList.h"
struct ssid { String essid; uint8_t channel=0,bssid[6]={}; bool selected=false; };
struct AccessPoint {
  String essid; uint8_t channel=0,bssid[6]={}; bool selected=false;
  char beacon[2]={}; int8_t rssi=0; LinkedList<uint16_t>* stations=nullptr;
  uint16_t packets=0; uint8_t sec=0; bool wps=false; String man;
  bool has_msg_1=false,has_msg_2=false,has_msg_3=false,has_msg_4=false;
  uint32_t last_seen_ms=0;
};
struct Station { uint8_t mac[6]={}; bool selected=false; uint16_t packets=0,ap=0; uint32_t last_seen_ms=0; };
namespace marauder {
class RuntimeMemoryGuard {
 public:
  int remaining=-1;
  static RuntimeMemoryGuard& instance() { static RuntimeMemoryGuard g; return g; }
  bool allow(size_t) { if(remaining<0) return true; if(!remaining) return false; --remaining; return true; }
};
}
static bool idle=true;
static bool dashboardTargetsIdle() { return idle; }
static void yield() {}
struct FakeSerial { void printf(const char*,...) {} };
static FakeSerial Serial;
static const char* FILE_READ="rb";
static const char* FILE_WRITE="wb";
static int writeBudget=-1, renameFailure=-1;
class File {
  FILE* file_=nullptr;
 public:
  File()=default;
  explicit File(FILE* p):file_(p) {}
  explicit operator bool() const { return file_; }
  size_t size() {
    if(!file_) return 0;
    long position=ftell(file_); fseek(file_,0,SEEK_END); long n=ftell(file_); fseek(file_,position,SEEK_SET); return n;
  }
  int available() { return file_&&static_cast<size_t>(ftell(file_))<size(); }
  int read() { return fgetc(file_); }
  size_t write(const uint8_t* p,size_t n) {
    if(writeBudget>=0&&n>static_cast<size_t>(writeBudget)) n=writeBudget;
    size_t written=fwrite(p,1,n,file_); if(writeBudget>=0) writeBudget-=written; return written;
  }
  size_t write(uint8_t c) { return write(&c,1); }
  void flush() { fflush(file_); }
  void close() { if(file_) fclose(file_); file_=nullptr; }
};
struct FakeFS {
  size_t total=1024*1024, used=0;
  void path(const char* p,char* out) { snprintf(out,256,".cache/storage_fs%s",p); }
  size_t totalBytes() { return total; }
  size_t usedBytes() { return used; }
  bool exists(const char* p) { char f[256]; path(p,f); struct stat info; return stat(f,&info)==0; }
  bool remove(const char* p) { char f[256]; path(p,f); return ::remove(f)==0; }
  bool rename(const char* a,const char* b) {
    if(renameFailure>=0) { if(!renameFailure) { renameFailure=-1; return false; } --renameFailure; }
    char source[256],dest[256]; path(a,source); path(b,dest); return ::rename(source,dest)==0;
  }
  File open(const char* p,const char* mode) { char f[256]; path(p,f); return File(fopen(f,mode)); }
};
static FakeFS SPIFFS;
