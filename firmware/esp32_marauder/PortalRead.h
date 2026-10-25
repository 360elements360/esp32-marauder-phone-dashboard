#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
// Preserve UTF-8, line endings and tabs. Partial reads are allowed, truncation
// and embedded NULs fail before a caller publishes the replacement page.
template<class Reader> bool portalReadExact(Reader& reader,char* output,size_t size) {
  size_t done=0;
  while(done<size) {
    const size_t count=reader.read(reinterpret_cast<uint8_t*>(output+done),size-done);
    if(!count || count>size-done)return false;
    if(memchr(output+done,0,count))return false;
    done+=count;
  }
  output[size]=0;return true;
}
