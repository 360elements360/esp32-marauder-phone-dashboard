#include "../firmware/esp32_marauder/ToolCore.h"
#include <assert.h>
#include <stdio.h>
#include "../firmware/esp32_marauder/PortalRead.h"
struct PortalReader {
 const char* data;size_t left;size_t chunk;
 size_t read(uint8_t* out,size_t count){size_t n=count<left?count:left;if(n>chunk)n=chunk;memcpy(out,data,n);data+=n;left-=n;return n;}
};
int main() {
  const char html[]="<html>\n<script>// comment\nalert('caf\xc3\xa9');\n</script>\t</html>";
  char copied[128];PortalReader reader{html,sizeof(html)-1,3};
  assert(portalReadExact(reader,copied,sizeof(html)-1));assert(!memcmp(copied,html,sizeof(html)));
  PortalReader shortRead{html,2,1};assert(!portalReadExact(shortRead,copied,10));
  const char nullHtml[]={'a',0,'b'};PortalReader nul{nullHtml,3,3};assert(!portalReadExact(nul,copied,3));
  assert(toolSafeText("hello world", 32));
  assert(!toolSafeJson("bad\\u0000"));
  assert(toolPublicHtml("index.html"));
  assert(!toolPublicHtml("../index.html"));
  assert(!toolPublicHtml("settings.json"));
  assert(!toolSafeText("x\" -a 3", 32));
  assert(!toolSafeText("x\nhelp", 32));
  assert(!toolSafeText("ab", 1));
  assert(toolSafeText("it's fine", 32));
  assert(toolValidIp("192.168.4.1"));
  assert(!toolValidIp("192.168.4.999"));
  assert(!toolValidIp("1.2.3"));
  char id[49]; toolContentIdentity("ssid", "hello", id);
  assert(toolContentMatches("ssid", "hello", id));
  assert(!toolContentMatches("ssid", "changed", id));
  ToolArgument args[2] = {};
  workflowCopy(args[0].name, sizeof(args[0].name), "target");
  workflowCopy(args[0].value, sizeof(args[0].value), "aa:bb:cc:dd:ee:ff");
  workflowCopy(args[0].indices, sizeof(args[0].indices), "4");
  args[0].target = true; args[0].apIndex = 2;
  workflowCopy(args[1].name, sizeof(args[1].name), "password");
  workflowCopy(args[1].value, sizeof(args[1].value), "two words"); args[1].quoted = true;
  char out[257];
  assert(toolRender("join -a {target.index} -p {password}", args, 2, 5, 10, out, sizeof(out)));
  assert(!strcmp(out, "join -a 4 -p \"two words\""));
  assert(toolRender("sigstren -s {target.apIndex} {target.index}", args, 2, 5, 10, out, sizeof(out)));
  assert(!strcmp(out, "sigstren -s 2 4"));
  assert(!toolRender("help {missing}", args, 2, 1, 2, out, sizeof(out)));
  assert(!toolRender("help {password.index}", args, 2, 1, 2, out, sizeof(out)));
  assert(!toolRender("help {password}", args, 2, 1, 2, out, 5));
  WorkflowCore job;
  assert(toolSequenceBudget(false, 1) == 30);
  assert(toolSequenceBudget(true, 8) == 8);
  assert(job.queueTool("network_join", toolSequenceBudget(false, 1), 0, 100));
  job.running(100); assert(!job.expired(30099)); assert(job.expired(30100));
  job.finish(WorkflowState::Failed, "deadline");
  assert(job.queueTool("device_info", 5, 0, 0xfffffff0));
  assert(!job.queueTool("other", 5, 0, 2));
  job.running(0xfffffff0); assert(!job.expired(10)); assert(job.expired(6000));
  assert(!job.cancel(job.id+1,6001)); assert(job.cancel(job.id,6001));
  puts("tool core: bounded rendering, injection rejection, content identities, IP validation, exact cancellation and rollover passed");
}
