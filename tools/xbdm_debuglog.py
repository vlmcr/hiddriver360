"""Stream DbgPrint output from an Xbox 360 running xbdm.xex, using the XDK's xbdm.dll.

Usage:
    python xbdm_debuglog.py <console ip> [seconds] [filter]

Prints every debug string the console emits (DM_DEBUGSTR notifications). With a
filter, only lines containing that text are shown (default: everything).
Needs the Xbox 360 XDK installed (uses bin\\x64\\xbdm.dll on 64 bit Python).
"""
import ctypes
import os
import sys
import time

XDK = os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")
arch = "x64" if ctypes.sizeof(ctypes.c_void_p) == 8 else "win32"
dll_path = os.path.join(XDK, "bin", arch, "xbdm.dll")
xbdm = ctypes.WinDLL(dll_path)

HRESULT = ctypes.c_long
DM_DEBUGSTR = 2
DM_PERSISTENT = 0x00000001
DM_DEBUGSESSION = 0x00000002

PDM_NOTIFY_FUNCTION = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p)


class DMN_DEBUGSTR(ctypes.Structure):
    _fields_ = [("ThreadId", ctypes.c_ulong), ("Length", ctypes.c_ulong), ("String", ctypes.c_char_p)]


xbdm.DmSetXboxNameNoRegister.argtypes = [ctypes.c_char_p]
xbdm.DmSetXboxNameNoRegister.restype = HRESULT
xbdm.DmSetConnectionTimeout.argtypes = [ctypes.c_ulong, ctypes.c_ulong]
xbdm.DmSetConnectionTimeout.restype = HRESULT
xbdm.DmOpenNotificationSession.argtypes = [ctypes.c_ulong, ctypes.POINTER(ctypes.c_void_p)]
xbdm.DmOpenNotificationSession.restype = HRESULT
xbdm.DmCloseNotificationSession.argtypes = [ctypes.c_void_p]
xbdm.DmCloseNotificationSession.restype = HRESULT
xbdm.DmNotify.argtypes = [ctypes.c_void_p, ctypes.c_ulong, PDM_NOTIFY_FUNCTION]
xbdm.DmNotify.restype = HRESULT
xbdm.DmGetXboxName.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_ulong)]
xbdm.DmGetXboxName.restype = HRESULT
xbdm.DmOpenConnection.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
xbdm.DmOpenConnection.restype = HRESULT
xbdm.DmCloseConnection.argtypes = [ctypes.c_void_p]
xbdm.DmCloseConnection.restype = HRESULT
xbdm.DmSendCommand.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_ulong)]
xbdm.DmSendCommand.restype = HRESULT

lines = []
flt = None


@PDM_NOTIFY_FUNCTION
def on_notify(kind, param):
    if kind == DM_DEBUGSTR and param:
        s = ctypes.cast(param, ctypes.POINTER(DMN_DEBUGSTR)).contents
        text = ctypes.string_at(s.String, s.Length).decode("latin-1").rstrip("\r\n")
        if not flt or flt.lower() in text.lower():
            stamp = time.strftime("%H:%M:%S")
            print("%s %s" % (stamp, text), flush=True)
            lines.append(text)
    return 0


def check(hr, what):
    if hr < 0:
        sys.exit("%s failed: HRESULT 0x%08X" % (what, hr & 0xFFFFFFFF))


def main():
    global flt
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.111"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    flt = sys.argv[3] if len(sys.argv) > 3 else None

    check(xbdm.DmSetXboxNameNoRegister(ip.encode()), "DmSetXboxNameNoRegister")
    xbdm.DmSetConnectionTimeout(5000, 5000)

    # sanity: talk to xbdm once so we fail fast if it is not loaded
    conn = ctypes.c_void_p()
    check(xbdm.DmOpenConnection(ctypes.byref(conn)), "DmOpenConnection (is xbdm.xex loaded?)")
    resp = ctypes.create_string_buffer(512)
    n = ctypes.c_ulong(512)
    hr = xbdm.DmSendCommand(conn, b"dbgname", resp, ctypes.byref(n))
    print("connected to %s, xbdm says: %s (hr=0x%08X)" % (ip, resp.value.decode(errors="replace"), hr & 0xFFFFFFFF))
    xbdm.DmCloseConnection(conn)

    session = ctypes.c_void_p()
    check(xbdm.DmOpenNotificationSession(DM_PERSISTENT, ctypes.byref(session)), "DmOpenNotificationSession")
    check(xbdm.DmNotify(session, DM_DEBUGSTR, on_notify), "DmNotify(DM_DEBUGSTR)")
    print("listening for debug output for %.0f s%s ..." % (seconds, (" (filter: %s)" % flt) if flt else ""), flush=True)
    try:
        end = time.time() + seconds
        while time.time() < end:
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass
    finally:
        xbdm.DmCloseNotificationSession(session)
    print("captured %d line(s)" % len(lines))


if __name__ == "__main__":
    main()
