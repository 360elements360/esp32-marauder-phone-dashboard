"""Compile the actual CSI Wi-Fi lease methods against a model of pinned Arduino ownership."""
from pathlib import Path
import shutil, subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'firmware/esp32_marauder/CsiMotionMonitor.cpp').read_text()
def method(name):
 start=source.index(name);brace=source.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
code=r'''
#include <assert.h>
#include <stdint.h>
#define MARAUDER_DASHBOARD 1
#define CONFIG_SOC_WIFI_HE_SUPPORT 0
#define ESP_IDF_VERSION_MAJOR 5
#define WIFI_CSI_ENABLED 1
#define WIFI_SCAN_OFF 0
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_STA WIFI_MODE_STA
#define WIFI_SECOND_CHAN_NONE 0
#define ESP_OK 0
#define ESP_ERR_WIFI_INIT_STATE 1
using esp_err_t=int;
struct wifi_init_config_t {bool csi_enable=true;};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_csi_config_t {bool lltf_en,htltf_en,stbc_htltf2_en,ltf_merge_en,channel_filter_en,manu_scale,dump_ack_en;int shift;};
struct wifi_csi_info_t {};
bool driver=true, failCsi=false;int actualMode=WIFI_MODE_AP;
int esp_wifi_init(wifi_init_config_t*){if(driver)return ESP_ERR_WIFI_INIT_STATE;driver=true;return ESP_OK;}
int esp_wifi_stop(){return driver?ESP_OK:2;}
int esp_wifi_deinit(){driver=false;actualMode=WIFI_MODE_NULL;return ESP_OK;}
int esp_wifi_set_mode(int mode){if(!driver)return 2;actualMode=mode;return ESP_OK;}
int esp_wifi_start(){return driver?ESP_OK:2;}
int esp_wifi_set_channel(int,int){return driver?ESP_OK:2;}
int esp_wifi_set_promiscuous(bool){return driver?ESP_OK:2;}
int esp_wifi_set_csi_rx_cb(void (*)(void*,wifi_csi_info_t*),void*){return driver?ESP_OK:2;}
int esp_wifi_set_csi_config(wifi_csi_config_t*){return driver?ESP_OK:2;}
int esp_wifi_set_csi(bool enabled){return driver && !(enabled&&failCsi)?ESP_OK:2;}
struct ArduinoOwner {
 bool lowLevelInitDone=true, started=true;
 bool mode(int mode){
  int current=lowLevelInitDone&&started&&driver?actualMode:WIFI_MODE_NULL;
  if(current==mode)return true;
  if(!current&&mode&&!lowLevelInitDone){driver=true;lowLevelInitDone=true;}
  if(current&&!mode){esp_wifi_stop();esp_wifi_deinit();started=lowLevelInitDone=false;return true;}
  if(esp_wifi_set_mode(mode)!=ESP_OK)return false;
  started=true;return esp_wifi_start()==ESP_OK;
 }
} WiFi;
struct Scanner {int currentScanMode=0;bool wifi_initialized=true;void StopScan(int){currentScanMode=0;}} wifi_scan_obj;
class CsiMotionMonitor {public:bool wifi_owned_=false;int channel_=1;bool takeWiFi();void releaseWiFi();static void callback(void*,wifi_csi_info_t*){}};
void resetAP(){driver=true;actualMode=WIFI_MODE_AP;WiFi.lowLevelInitDone=WiFi.started=true;failCsi=false;}
'''
code+='\n'+method('bool CsiMotionMonitor::takeWiFi()')+'\n'+method('void CsiMotionMonitor::releaseWiFi()')
code+=r'''
int main(){
 resetAP();CsiMotionMonitor monitor;assert(monitor.takeWiFi());monitor.releaseWiFi();assert(WiFi.mode(WIFI_MODE_AP));assert(driver&&actualMode==WIFI_MODE_AP);
 resetAP();failCsi=true;assert(!monitor.takeWiFi());monitor.releaseWiFi();assert(WiFi.mode(WIFI_MODE_AP));
 resetAP();for(int i=0;i<5;++i){assert(monitor.takeWiFi());monitor.releaseWiFi();assert(WiFi.mode(WIFI_MODE_AP));}
}
'''
cache=ROOT/'.cache';cache.mkdir(exist_ok=True);test=cache/'test_csi_wifi_ownership.cpp';test.write_text(code)
output=cache/('test_csi_wifi_ownership.exe' if sys.platform=='win32' else 'test_csi_wifi_ownership')
compiler=shutil.which('g++');command=[compiler] if compiler else [shutil.which('uv') or 'uv','run','--with','ziglang==0.14.1','python','-m','ziglang','c++']
subprocess.run(command+['-std=c++17','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-nostdlib++',str(test),'-o',str(output)],check=True)
subprocess.run([str(output)],check=True)
print('PASS: actual CSI take/release preserves Arduino driver ownership after normal stop, partial start failure, and repeated AP recovery')
