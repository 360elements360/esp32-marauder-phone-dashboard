"""Browser tests exercise production UI; routes stand in for the physical ESP32."""
from contextlib import contextmanager
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import re
import threading
import unittest
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]

class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *_): pass
    def do_GET(self):
        if self.path == '/api/stalled':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', '512')
            self.end_headers()
            self.wfile.write(b'{"board":'); self.wfile.flush()
            threading.Event().wait(20)
            return
        super().do_GET()

class DashboardBrowserTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(ROOT / "web")))
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.playwright = sync_playwright().start()
        chrome = Path("C:/Program Files/Google/Chrome/Application/chrome.exe")
        cls.browser = cls.playwright.chromium.launch(executable_path=str(chrome) if chrome.exists() else None, headless=True)

    @classmethod
    def tearDownClass(cls):
        cls.browser.close(); cls.playwright.stop(); cls.server.shutdown(); cls.server.server_close()

    def setUp(self):
        self.page = self.browser.new_page(viewport={"width": 1280, "height": 900})
        self.page.set_default_timeout(7000)
        self.commands = []; self.api_calls = 0; self.errors = []
        self.page.on("pageerror", lambda e: self.errors.append(str(e)))
        self.queue_full = False
        self.files = [{"path":"/capture.pcap", "size":1234}]
        self.deleted = []
        self.page.route("**/api/**", self.route)
        self.page.goto(f"http://127.0.0.1:{self.server.server_port}/")

    def tearDown(self): self.page.close()

    def route(self, route):
        self.api_calls += 1
        if route.request.headers.get("x-device-key") != "test-password":
            return route.fulfill(status=401, json={"error":"Wrong device password"})
        path = route.request.url.split("/api/")[1].split("?")[0]
        if path == "status":
            return route.fulfill(json={"version":"v1.19.2", "board":"ESP32-S3 N16R8", "bootId":42,
                "uptime":123, "heap":180000, "minHeap":160000, "psram":8000000, "flash":16777216,
                "mode":0, "channel":1, "remaining":0, "queued":0, "ssid":"Marauder-TEST", "clients":1,
                "sd":False, "gps":False, "screen":False, "logCapacity":8192})
        if path == "log":
            cursor = int(route.request.url.split("cursor=")[1])
            return route.fulfill(json={"cursor":100, "lost":False, "text":"<img src=x onerror=alert(1)>\nReady\n" if cursor == 0 else ""})
        if path == "command":
            if self.queue_full: return route.fulfill(status=429, json={"error":"Command queue full"})
            self.commands.append(route.request.post_data_json)
            return route.fulfill(status=202, json={"message":"Queued"})
        if path == "files": return route.fulfill(json={"files":self.files,"limit":100})
        if path == "file/delete":
            path = route.request.post_data_json['path']
            self.deleted.append(path)
            self.files = [file for file in self.files if file['path'] != path]
            return route.fulfill(json={"message":"File deleted"})
        return route.fulfill(status=404, json={"error":"Not found"})

    def login(self):
        self.page.get_by_label("Device password").fill("test-password")
        self.page.get_by_role("button", name="Unlock dashboard").click()
        self.page.locator("#connection").filter(has_text=re.compile('^Connected$')).wait_for()

    def test_login_failure_and_actual_command_payload(self):
        self.page.get_by_label("Device password").fill("wrong-password")
        self.page.get_by_role("button", name="Unlock dashboard").click()
        self.page.get_by_text("Wrong device password", exact=True).wait_for()
        self.login()
        self.page.get_by_role("button", name="Advanced", exact=True).click()
        self.page.get_by_role("button", name="Console", exact=True).click()
        self.page.get_by_label("Marauder command").fill("list -a")
        self.page.get_by_role("button", name="Run command", exact=True).click()
        self.page.get_by_text("Queued for execution", exact=False).wait_for()
        self.assertEqual(self.commands, [{"command":"list -a", "seconds":30}])
        self.assertFalse(self.errors)

    def test_quick_scan_uses_the_existing_firmware_command(self):
        self.login()
        self.page.get_by_role('button', name='Advanced', exact=True).click()
        self.page.get_by_role('button', name='Scan access points', exact=False).click()
        self.page.get_by_text('Queued for execution', exact=False).wait_for()
        self.assertEqual(self.commands, [{'command':'sniffbeacon', 'seconds':30}])

    def test_queue_failure_preserves_input_and_never_retries(self):
        self.login(); self.queue_full = True
        self.page.get_by_role("button", name="Advanced", exact=True).click()
        self.page.get_by_role("button", name="Console", exact=True).click()
        command = self.page.get_by_label("Marauder command")
        command.fill("help")
        self.page.get_by_role("button", name="Run command", exact=True).click()
        self.page.get_by_text("Command queue full", exact=True).wait_for()
        self.assertEqual(command.input_value(), "help")
        self.page.wait_for_timeout(2300)
        self.assertEqual(self.commands, [])

    def test_untrusted_output_is_text_and_mobile_has_no_overflow(self):
        self.login()
        self.page.get_by_role("button", name="Advanced", exact=True).click()
        self.page.get_by_role("button", name="Console", exact=True).click()
        self.page.locator("#console").filter(has_text="<img src=x").wait_for()
        self.assertEqual(self.page.locator("#console img").count(), 0)
        self.page.set_viewport_size({"width":390, "height":844})
        self.assertTrue(self.page.evaluate("document.documentElement.scrollWidth <= innerWidth"))
        self.page.screenshot(path=str(ROOT / ".cache/dashboard-mobile.png"))

    def test_commands_catalog_and_files_use_real_interface(self):
        self.login()
        self.page.get_by_role("button", name="Advanced", exact=True).click()
        self.page.get_by_role("button", name="Commands", exact=True).click()
        self.page.get_by_label("Search commands").fill("sniffprobe")
        self.page.get_by_role("button", name="Use sniffprobe", exact=True).click()
        self.assertEqual(self.page.get_by_label("Marauder command").input_value(), "sniffprobe")
        self.page.get_by_role("button", name="Files", exact=True).click()
        self.page.get_by_text("/capture.pcap", exact=False).wait_for()
        self.assertTrue(self.page.locator('#tab-files').get_by_text("SD card unavailable", exact=False).is_visible())

    def test_file_deletion_requires_confirmation_and_refreshes_directory(self):
        self.login()
        self.page.get_by_role('button', name='Files', exact=True).click()
        self.page.get_by_text('/capture.pcap', exact=False).wait_for()
        self.page.once('dialog', lambda dialog: dialog.dismiss())
        self.page.get_by_role('button', name='Delete', exact=True).click()
        self.assertEqual(self.deleted, [])
        self.page.once('dialog', lambda dialog: dialog.accept())
        self.page.get_by_role('button', name='Delete', exact=True).click()
        self.page.get_by_text('No capture or log files', exact=False).wait_for()
        self.assertEqual(self.deleted, ['/capture.pcap'])

    def test_disconnection_is_visible_and_does_not_resubmit(self):
        self.login()
        self.page.unroute("**/api/**")
        self.page.route("**/api/**", lambda route: route.abort())
        self.page.locator("#connection").filter(has_text=re.compile('^Disconnected$')).wait_for(timeout=12000)
        self.assertEqual(self.commands, [])

    def test_reconnection_clears_the_disconnect_message(self):
        self.login()
        self.page.unroute("**/api/**")
        self.page.route("**/api/**", lambda route: route.abort())
        self.page.locator("#connection").filter(has_text=re.compile('^Disconnected$')).wait_for(timeout=12000)
        self.page.unroute("**/api/**")
        self.page.route("**/api/**", self.route)
        self.page.evaluate("window.dispatchEvent(new Event('online'))")
        self.page.locator("#connection").filter(has_text=re.compile('^Connected$')).wait_for()
        self.assertNotIn("Connection lost", self.page.locator('#message').inner_text())

    def test_packed_dashboard_runs_without_external_assets(self):
        self.page.route("**/packed", lambda route: route.fulfill(content_type="text/html", path=str(ROOT / '.cache/dashboard.html')))
        self.page.goto(f"http://127.0.0.1:{self.server.server_port}/packed")
        self.login()
        self.assertGreater(self.page.locator('#catalog .command-card').count(), 60)
        self.assertEqual(self.errors, [])
        self.page.screenshot(path=str(ROOT / '.cache/dashboard-desktop.png'))
        self.page.set_viewport_size({"width":390, "height":844})
        self.assertTrue(self.page.evaluate("document.documentElement.scrollWidth <= innerWidth"))
        self.page.screenshot(path=str(ROOT / '.cache/dashboard-overview-mobile.png'), full_page=True)

    def test_response_body_stall_does_not_freeze_reconnection(self):
        self.login()
        self.page.unroute('**/api/**')
        def stalled(route):
            if '/api/status' in route.request.url:
                route.continue_(url=f'http://127.0.0.1:{self.server.server_port}/api/stalled')
            else: self.route(route)
        self.page.route('**/api/**', stalled)
        self.page.evaluate("window.dispatchEvent(new Event('online'))")
        self.page.locator('#connection').filter(has_text=re.compile('^Disconnected$')).wait_for(timeout=10000)

    def test_hidden_page_stops_polling(self):
        self.login()
        self.page.evaluate("Object.defineProperty(document, 'hidden', {value:true, configurable:true}); document.dispatchEvent(new Event('visibilitychange'))")
        before = self.api_calls
        self.page.wait_for_timeout(2300)
        self.assertEqual(self.api_calls, before)

if __name__ == "__main__": unittest.main(verbosity=2)
