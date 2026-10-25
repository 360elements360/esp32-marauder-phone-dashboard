#include "../firmware/esp32_marauder/DashboardStorageFormat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace dashboard_storage;
int main() {
  Record r{};
  assert(parseRecord("A aa:bb:cc:dd:ee:ff 14 -127 255 65535 1 410a2042", r));
  assert(r.type == 'A' && r.nameLength == 4 && r.name[1] == '\n' && r.channel == 14);
  char line[kLineCapacity]; assert(formatRecord(r,line,sizeof(line)));
  Record copy{}; assert(parseRecord(line,copy)); assert(!memcmp(copy.name,r.name,4));
  assert(!parseRecord("A aa:bb:cc:dd:ee:ff 0 -127 0 0 0 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:ff 15 -127 0 0 0 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:ff 1 -129 0 0 0 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:ff 1 -1 256 0 0 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:ff 1 -1 0 65536 0 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:ff 1 -1 0 0 2 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:f 1 -1 0 0 0 41",r));
  assert(!parseRecord("A aa:bb:cc:dd:ee:gg 1 -1 0 0 0 41",r));
  assert(!parseRecord("S aa:bb:cc:dd:ee:ff 1 414",r));
  assert(!parseRecord("S aa:bb:cc:dd:ee:ff 1 00",r));
  assert(parseRecord("S aa:bb:cc:dd:ee:ff 1 -",r) && r.nameLength == 0);
  assert(!parseRecord("S aa:bb:cc:dd:ee:ff 1 41 extra",r));
  assert(!parseRecord("S aa:bb:cc:dd:ee:ff +1 41",r));
  assert(parseRecord("T - 01:02:03:04:05:06 12",r) && !r.associated);
  assert(parseRecord("T aa:bb:cc:dd:ee:ff 01:02:03:04:05:06 12",r) && r.associated);
  char longName[66]; memset(longName,'4',65); longName[65]=0;
  snprintf(line,sizeof(line),"S aa:bb:cc:dd:ee:ff 1 %s",longName);
  assert(!parseRecord(line,r));
  uint32_t h = checksum(2166136261u, "S aa:bb:cc:dd:ee:ff 1 41");
  assert(h != checksum(2166136261u, "S aa:bb:cc:dd:ee:ff 1 42"));
  assert(parseFooter("E 4 4294967295",4,4294967295u));
  assert(!parseFooter("E 4 4294967296",4,0));
  assert(!parseFooter("E 4 1 trailing",4,1));
  assert(!parseFooter("E 5 1",4,1));
  puts("PASS: bounded target storage codec and malformed records");
}
