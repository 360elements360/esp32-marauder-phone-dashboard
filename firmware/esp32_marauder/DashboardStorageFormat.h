#pragma once
// Version one is a flat, bounded line format. Names are hex bytes (max 32),
// so embedded whitespace is preserved without escapes or nested allocations.
// Header: MTD1 AP or MTD1 SSID. A MAC channel RSSI security packets WPS hexName;
// S MAC channel hexName; T parentMAC-or-dash stationMAC packets. Empty name: dash.
// All AP records precede stations. Footer: E recordCount FNV1a32. Every line ends
// in LF. FNV covers each record including LF; it detects incomplete writes, and
// structural validation remains required. Only these two fixed files are read.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
namespace dashboard_storage {
constexpr size_t kLineCapacity = 160;
constexpr size_t kMaxAps = 256, kMaxSsids = 512, kMaxStations = 1024;
constexpr size_t kMaxFileBytes = 256 * 1024;
struct Record {
  char type = 0;
  uint8_t mac[6] = {}, parent[6] = {}, channel = 0, security = 0;
  int8_t rssi = 0;
  uint16_t packets = 0;
  bool wps = false, associated = false;
  char name[33] = {};
  size_t nameLength = 0;
};
inline int nibble(char c) {
  if(c >= '0' && c <= '9') return c-'0';
  if(c >= 'a' && c <= 'f') return c-'a'+10;
  if(c >= 'A' && c <= 'F') return c-'A'+10;
  return -1;
}
inline bool mac(const char* s,uint8_t* out) {
  if(strlen(s)!=17) return false;
  for(size_t i=0;i<6;++i) {
    int a=nibble(s[i*3]),b=nibble(s[i*3+1]);
    if(a<0||b<0||(i<5&&s[i*3+2]!=':')) return false;
    out[i]=static_cast<uint8_t>(a*16+b);
  }
  return true;
}
inline void formatMac(const uint8_t* m,char* out) {
  snprintf(out,18,"%02x:%02x:%02x:%02x:%02x:%02x",m[0],m[1],m[2],m[3],m[4],m[5]);
}
inline bool number(const char* s,uint32_t max,uint32_t& out) {
  if(!*s) return false;
  uint32_t n=0;
  for(;*s;++s) {
    if(*s<'0'||*s>'9') return false;
    uint32_t d=static_cast<uint32_t>(*s-'0');
    if(d>max||n>(max-d)/10) return false;
    n=n*10+d;
  }
  out=n; return true;
}
inline bool name(const char* s,Record& r) {
  if(!strcmp(s,"-")) return true;
  size_t len=strlen(s);
  if(!len||len>64||len%2) return false;
  for(size_t i=0;i<len/2;++i) {
    int a=nibble(s[i*2]),b=nibble(s[i*2+1]);
    if(a<0||b<0||!(a*16+b)) return false; // Arduino names cannot contain NUL.
    r.name[i]=static_cast<char>(a*16+b);
  }
  r.nameLength=len/2; return true;
}
inline size_t tokens(char* s,char** out,size_t capacity) {
  size_t n=0;
  while(*s) {
    if(*s==' '||n==capacity) return 0;
    out[n++]=s;
    while(*s&&*s!=' ') ++s;
    if(*s) { *s++=0; if(!*s) return 0; }
  }
  return n;
}
inline bool parseRecord(const char* text,Record& r) {
  if(strlen(text)>=kLineCapacity) return false;
  char line[kLineCapacity]; strcpy(line,text);
  char* t[9]; size_t n=tokens(line,t,9); r=Record{};
  if(!n||strlen(t[0])!=1) return false;
  r.type=t[0][0]; uint32_t value;
  if(r.type=='T') {
    if(n!=4||!mac(t[2],r.mac)||!number(t[3],65535,value)) return false;
    r.packets=static_cast<uint16_t>(value);
    r.associated=strcmp(t[1],"-")!=0;
    return !r.associated||mac(t[1],r.parent);
  }
  if((r.type!='A'||n!=8)&&(r.type!='S'||n!=4)) return false;
  if(!mac(t[1],r.mac)||!number(t[2],14,value)||value<1) return false;
  r.channel=static_cast<uint8_t>(value);
  if(r.type=='S') return name(t[3],r);
  const char* signedRssi=t[3]; bool negative=*signedRssi=='-';
  if(negative) ++signedRssi;
  if(!number(signedRssi,negative?128:127,value)) return false;
  r.rssi=static_cast<int8_t>(negative?-static_cast<int>(value):static_cast<int>(value));
  if(!number(t[4],255,value)) return false;
  r.security=static_cast<uint8_t>(value);
  if(!number(t[5],65535,value)) return false;
  r.packets=static_cast<uint16_t>(value);
  if(!number(t[6],1,value)) return false;
  r.wps=value!=0; return name(t[7],r);
}
inline bool formatRecord(const Record& r,char* out,size_t capacity) {
  if(r.nameLength>32) return false;
  char m[18],p[18],encoded[65]; formatMac(r.mac,m); formatMac(r.parent,p);
  static const char hex[]="0123456789abcdef";
  for(size_t i=0;i<r.nameLength;++i) {
    unsigned char c=static_cast<unsigned char>(r.name[i]); if(!c) return false;
    encoded[i*2]=hex[c>>4]; encoded[i*2+1]=hex[c&15];
  }
  encoded[r.nameLength*2]=0; const char* text=r.nameLength?encoded:"-";
  int length=-1;
  if(r.type=='S') length=snprintf(out,capacity,"S %s %u %s",m,r.channel,text);
  if(r.type=='A') length=snprintf(out,capacity,"A %s %u %d %u %u %u %s",m,r.channel,r.rssi,r.security,r.packets,r.wps?1:0,text);
  if(r.type=='T') length=snprintf(out,capacity,"T %s %s %u",r.associated?p:"-",m,r.packets);
  if(length<0||static_cast<size_t>(length)>=capacity) return false;
  Record checked; return parseRecord(out,checked);
}
inline uint32_t checksum(uint32_t hash,const char* text) {
  for(;*text;++text) { hash^=static_cast<uint8_t>(*text); hash*=16777619u; }
  hash^='\n'; return hash*16777619u;
}
inline bool parseFooter(const char* text,uint32_t count,uint32_t hash) {
  if(strlen(text)>=kLineCapacity) return false;
  char line[kLineCapacity]; strcpy(line,text); char* t[3]; uint32_t c,h;
  return tokens(line,t,3)==3&&!strcmp(t[0],"E")&&number(t[1],kMaxAps+kMaxStations+kMaxSsids,c)&&number(t[2],UINT32_MAX,h)&&c==count&&h==hash;
}
}
