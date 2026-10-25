#include "../firmware/esp32_marauder/DashboardAccess.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main() {
  DashboardAccess access;
  assert(access.setupRequired());
  assert(!strcmp(access.password(), "marauder-setup"));
  access.load("1234567890abcdef", 16);
  assert(!access.setupRequired() && !strcmp(access.password(), "1234567890abcdef"));
  assert(!access.canChange("short", 5));
  assert(!access.canChange(" password", 9));
  assert(!access.canChange("password ", 9));
  assert(!access.canChange("marauder-setup", 14));
  assert(!access.canChange("1234567890abcdef", 16));
  assert(!access.canChange("pass\0word", 9));
  assert(!access.canChange("password\n", 9));
  assert(!access.canChange("passw\xc3\xb6rd", 9));
  char longest[65]; memset(longest, 'x', 64); longest[64] = 0;
  assert(!access.canChange(longest, 64));
  assert(access.canChange(longest, 63));
  assert(access.stage("Phone pass!", 11, 0xfffffff0u));
  assert(access.changing() && !access.canChange("another-pass", 12));
  assert(!access.apply(1483));
  assert(!strcmp(access.password(), "1234567890abcdef"));
  assert(access.apply(1484) && !access.changing());
  assert(!strcmp(access.password(), "Phone pass!"));
  DashboardAccess rebooted;
  rebooted.load(access.password(), strlen(access.password()));
  assert(!rebooted.setupRequired() && !strcmp(rebooted.password(), "Phone pass!"));
  rebooted.load("bad", 3);
  assert(rebooted.setupRequired());
  assert(rebooted.stage(longest, 63, 0));
  assert(rebooted.apply(1500) && strlen(rebooted.password()) == 63);
  puts("PASS: phone setup, existing credential migration, password bounds/control bytes, reserved/default password, delayed rotation, rollover and reload");
}
