"""Connect an already saved Windows Wi-Fi profile without enumerating nearby SSIDs.

Uses the documented WlanConnect profile API. Does not change Location settings.
Callers can supply an authenticated readiness probe to preserve a recovered link.
"""
import ctypes as c
from ctypes import wintypes as w
import os
import time

class GUID(c.Structure): _fields_ = [('bytes', c.c_ubyte * 16)]
class Interface(c.Structure):
    _fields_ = [('guid',GUID),('description',w.WCHAR * 256),('state',w.DWORD)]
class Parameters(c.Structure):
    _fields_ = [('mode',w.DWORD),('profile',w.LPCWSTR),('ssid',c.c_void_p),('bssids',c.c_void_p),('bssType',w.DWORD),('flags',w.DWORD)]

def connect_profile(profile, timeout=30, ready=None):
    if os.name != 'nt': raise RuntimeError('Automatic profile reconnection is Windows-only; reconnect manually')
    api = c.WinDLL('wlanapi')
    api.WlanOpenHandle.argtypes = [w.DWORD,c.c_void_p,c.POINTER(w.DWORD),c.POINTER(w.HANDLE)]
    api.WlanEnumInterfaces.argtypes = [w.HANDLE,c.c_void_p,c.POINTER(c.c_void_p)]
    api.WlanConnect.argtypes = [w.HANDLE,c.POINTER(GUID),c.POINTER(Parameters),c.c_void_p]
    api.WlanDisconnect.argtypes = [w.HANDLE,c.POINTER(GUID),c.c_void_p]
    api.WlanCloseHandle.argtypes = [w.HANDLE,c.c_void_p]
    api.WlanFreeMemory.argtypes = [c.c_void_p]
    version, handle = w.DWORD(), w.HANDLE()
    result = api.WlanOpenHandle(2,None,c.byref(version),c.byref(handle))
    if result: raise OSError(result, 'WlanOpenHandle rejected')
    def interfaces():
        pointer = c.c_void_p()
        result = api.WlanEnumInterfaces(handle,None,c.byref(pointer))
        if result: raise OSError(result, 'WlanEnumInterfaces rejected')
        try:
            count = c.cast(pointer,c.POINTER(w.DWORD))[0]
            if count != 1: raise RuntimeError(f'Expected one Wi-Fi interface, found {count}; connect manually')
            info = c.cast(pointer.value + 8,c.POINTER(Interface))[0]
            return GUID.from_buffer_copy(bytes(info.guid)), info.state
        finally: api.WlanFreeMemory(pointer)
    try:
        guid, _ = interfaces()
        if ready and ready(): return
        guid, state = interfaces()
        if ready and state == 1:
            # A connected flag and old DHCP lease can survive an AP restart.
            # Probe twice before discarding it so a newly recovered link wins.
            if ready(): return
            result = api.WlanDisconnect(handle,c.byref(guid),None)
            if result: raise OSError(result, 'Stale Wi-Fi disconnect rejected')
            time.sleep(.5)
        # Windows may have associated while the caller's HTTP request timed out.
        # Do not tear down that connection: WlanConnect can select the saved
        # profile directly, including when automatic association is in progress.
        parameters = Parameters(0,profile,None,None,1,0)
        result = api.WlanConnect(handle,c.byref(guid),c.byref(parameters),None)
        if result: raise OSError(result, 'Saved-profile connection rejected')
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            _, state = interfaces()
            if state == 1 and (not ready or ready()): return
            time.sleep(.5)
        raise TimeoutError('Windows did not finish connecting to the saved device profile')
    finally: api.WlanCloseHandle(handle,None)
