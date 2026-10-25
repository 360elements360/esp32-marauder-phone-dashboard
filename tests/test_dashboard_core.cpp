#include "../firmware/esp32_marauder/DashboardCore.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

struct SerialSink {
  bool online = true;
  int freeBytes = 0;
  size_t sent = 0;
  operator bool() const { return online; }
  int availableForWrite() const { return freeBytes; }
  size_t write(const uint8_t*, size_t length) {
    assert(length <= static_cast<size_t>(freeBytes));
    freeBytes -= length; sent += length; return length;
  }
};

int main() {
  DashboardLog<8> log;
  char out[9] = {};
  bool lost = false;
  uint32_t cursor = 0;
  log.append("abc", 3);
  assert(log.read(cursor, out, 8, lost) == 3 && !lost);
  assert(strcmp(out, "abc") == 0 && cursor == 3);
  log.append("defghijkl", 9);
  assert(log.read(cursor, out, 8, lost) == 8 && lost);
  assert(strcmp(out, "efghijkl") == 0 && cursor == 12);
  cursor = 100; // browser kept a cursor from before a device reboot
  assert(log.read(cursor, out, 3, lost) == 3 && lost);
  assert(strcmp(out, "efg") == 0 && cursor == 7);
  assert(log.read(cursor, out, 8, lost) == 5 && !lost);
  assert(strcmp(out, "hijkl") == 0 && cursor == 12);

  DashboardQueue queue;
  assert(!queue.push("", 30));
  assert(!queue.push("help\nreboot", 30));
  assert(!queue.push("help\rreboot", 30));
  assert(!queue.push("help\t", 30));
  char longCommand[258];
  memset(longCommand, 'x', 257); longCommand[257] = 0;
  assert(!queue.push(longCommand, 30));
  assert(!queue.push("help", 3601));
  assert(queue.push("help", 30));
  assert(queue.push("settings", 0));
  assert(queue.push("list -a", 5));
  assert(queue.push("stopscan", 0));
  assert(!queue.push("reboot", 0));
  DashboardCommand command;
  assert(queue.pop(command) && strcmp(command.text, "help") == 0 && command.seconds == 30);
  assert(queue.pop(command) && strcmp(command.text, "settings") == 0 && command.seconds == 0);
  assert(queue.pop(command) && strcmp(command.text, "list -a") == 0);
  assert(queue.pop(command) && strcmp(command.text, "stopscan") == 0);
  assert(!queue.pop(command));

  DashboardTimer timer;
  timer.start(0xfffffff0u, 1);
  assert(!timer.expired(0x20));
  assert(timer.expired(984)); // millis wrap must not lose the recovery timer
  timer.clear(); assert(!timer.expired(1000));
  timer.start(500, 0); assert(!timer.expired(999999));
  timer.start(1000, 30);
  timer.afterCommand(5000, 0, 7, 7, true);
  assert(timer.remaining(5000) == 26); // read-only queries cannot remove a radio deadline
  timer.afterCommand(6000, 10, 7, 8, true);
  assert(timer.remaining(6000) == 10);
  timer.afterCommand(7000, 30, 8, 9, false);
  assert(timer.remaining(7000) == 0);
  assert(dashboardSafePath("/captures/test.pcap"));
  assert(!dashboardSafePath("/../settings.json"));
  assert(!dashboardSafePath("/settings.json"));
  assert(!dashboardSafePath("/folder\\secret"));
  assert(!dashboardSafePath("relative"));
  size_t requestLength = 0;
  assert(dashboardInspectHttp("POST /api/command HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 999999\r\n\r\n", requestLength) == 413);
  assert(dashboardInspectHttp("POST /api/command HTTP/1.1\r\nContent-Type: multipart/form-data\r\nContent-Length: 10\r\n\r\n", requestLength) == 415);
  assert(dashboardInspectHttp("POST /api/command HTTP/1.1\r\nContent-Length: -1\r\n\r\n", requestLength) == 400);
  assert(dashboardInspectHttp("POST /api/command HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", requestLength) == 400);
  assert(dashboardInspectHttp("GET /api/status HTTP/1.1\r\nHost: board\r\n\r\n", requestLength) == 200);
  assert(requestLength == strlen("GET /api/status HTTP/1.1\r\nHost: board\r\n\r\n"));
  assert(dashboardInspectHttp("GET /api/status HTTP/1.1\r\nHost: board\r\n", requestLength) == 0);
  assert(dashboardInspectHttp("POST /api/command HTTP/1.1\r\nContent-Type: multipart/form-data\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n", requestLength) == 400);
  assert(dashboardInspectHttp("POST /unknown HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 0\r\n\r\n", requestLength) == 404);
  DashboardBody body;
  uint8_t half[256]; memset(half, 'x', sizeof(half));
  assert(body.append(half, 256));
  assert(body.append(half, 256) && body.size() == 512 && body.valid());
  assert(!body.append(half, 1) && !body.valid() && body.size() == 512);
  body.reset(); assert(body.valid() && body.size() == 0 && body.data()[0] == 0);
  SerialSink sink;
  assert(dashboardForwardSerial(sink, half, 256) == 0 && sink.sent == 0);
  sink.freeBytes = 12;
  assert(dashboardForwardSerial(sink, half, 256) == 12 && sink.sent == 12);
  sink.online = false; sink.freeBytes = 100;
  assert(dashboardForwardSerial(sink, half, 256) == 0 && sink.sent == 12);
  size_t templateLength=0;
  assert(dashboardInspectHttp("POST /api/access HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}", templateLength)==200);
  assert(dashboardInspectHttp("POST /api/access HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 513\r\n\r\n", templateLength)==413);
  assert(dashboardInspectHttp("POST /api/template HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}", templateLength)==200);
  assert(dashboardInspectHttp("POST /api/template HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 513\r\n\r\n", templateLength)==413);
  puts("PASS: ring overflow/reboot, queue bounds/FIFO, command validation, timer rollover/read-only deadlines, private paths, HTTP limits/duplicate headers, bounded raw body");
}
