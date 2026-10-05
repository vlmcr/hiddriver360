"""Dump the USB configuration descriptor of a connected device on Windows.

Reads descriptors through the USB hub (IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION),
the same way USBView does, so no driver changes are needed. Used to confirm the
interface / endpoint layout hiddriver360's GIP path expects.

Usage: python usb_descriptor_dump.py [VID] [PID]   (hex, defaults to 045E 0B12)
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys

VID = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0x045E
PID = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x0B12

setupapi = ctypes.windll.setupapi
kernel32 = ctypes.windll.kernel32

GUID_DEVINTERFACE_USB_HUB = ctypes.create_string_buffer(
    struct.pack("<IHH8s", 0xF18A0E88, 0xC30C, 0x11D0, bytes([0x88, 0x15, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0xD8])))
DIGCF_PRESENT = 0x02
DIGCF_DEVICEINTERFACE = 0x10
GENERIC_WRITE = 0x40000000
FILE_SHARE_WRITE = 0x2
OPEN_EXISTING = 3
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value

IOCTL_USB_GET_NODE_INFORMATION = 0x220408
IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX = 0x220448
IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION = 0x220410


class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [("cbSize", wt.DWORD), ("InterfaceClassGuid", ctypes.c_byte * 16),
                ("Flags", wt.DWORD), ("Reserved", ctypes.c_void_p)]


setupapi.SetupDiGetClassDevsA.restype = ctypes.c_void_p
setupapi.SetupDiEnumDeviceInterfaces.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p]
setupapi.SetupDiGetDeviceInterfaceDetailA.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p, ctypes.c_void_p]
kernel32.CreateFileA.restype = ctypes.c_void_p
kernel32.CreateFileA.argtypes = [ctypes.c_char_p, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
kernel32.DeviceIoControl.argtypes = [ctypes.c_void_p, wt.DWORD, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p, ctypes.c_void_p]
kernel32.CloseHandle.argtypes = [ctypes.c_void_p]


def hub_paths():
    hdev = setupapi.SetupDiGetClassDevsA(GUID_DEVINTERFACE_USB_HUB, None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
    paths = []
    i = 0
    while True:
        ifd = SP_DEVICE_INTERFACE_DATA()
        ifd.cbSize = ctypes.sizeof(ifd)
        if not setupapi.SetupDiEnumDeviceInterfaces(hdev, None, GUID_DEVINTERFACE_USB_HUB, i, ctypes.byref(ifd)):
            break
        needed = wt.DWORD(0)
        setupapi.SetupDiGetDeviceInterfaceDetailA(hdev, ctypes.byref(ifd), None, 0, ctypes.byref(needed), None)
        buf = ctypes.create_string_buffer(needed.value)
        struct.pack_into("<I", buf, 0, 8 if ctypes.sizeof(ctypes.c_void_p) == 8 else 5)
        if setupapi.SetupDiGetDeviceInterfaceDetailA(hdev, ctypes.byref(ifd), buf, needed.value, None, None):
            paths.append(buf.raw[4:].split(b"\0", 1)[0])
        i += 1
    return paths


def ioctl(h, code, inbuf, outlen):
    out = ctypes.create_string_buffer(max(outlen, len(inbuf)))
    ctypes.memmove(out, inbuf, len(inbuf))
    ret = wt.DWORD(0)
    ok = kernel32.DeviceIoControl(h, code, out, len(inbuf), out, len(out), ctypes.byref(ret), None)
    return out.raw[:ret.value] if ok else None


def parse_config(data):
    off = 0
    lines = []
    while off + 2 <= len(data):
        blen, btype = data[off], data[off + 1]
        if blen == 0:
            break
        d = data[off:off + blen]
        if btype == 0x02:
            wTotal, nIf, cfg, iCfg, attr, power = struct.unpack_from("<HBBBBB", d, 2)
            lines.append(f"CONFIG  total={wTotal} interfaces={nIf} value={cfg} attr=0x{attr:02x} power={power*2}mA")
        elif btype == 0x04:
            num, alt, neps, cls, sub, proto, iIf = struct.unpack_from("<BBBBBBB", d, 2)
            lines.append(f"  INTERFACE {num} alt={alt} endpoints={neps} class=0x{cls:02X} subclass=0x{sub:02X} protocol=0x{proto:02X}")
        elif btype == 0x05:
            addr, attr, mps, interval = struct.unpack_from("<BBHB", d, 2)
            kind = ["control", "isochronous", "bulk", "interrupt"][attr & 3]
            direction = "IN " if addr & 0x80 else "OUT"
            lines.append(f"    ENDPOINT 0x{addr:02X} {direction} {kind} maxPacket={mps & 0x7FF} interval={interval}")
        else:
            lines.append(f"    (descriptor type 0x{btype:02X}, {blen} bytes)")
        off += blen
    return "\n".join(lines)


def main():
    found = False
    for path in hub_paths():
        h = kernel32.CreateFileA(path, GENERIC_WRITE, FILE_SHARE_WRITE, None, OPEN_EXISTING, 0, None)
        if h == INVALID_HANDLE_VALUE or h is None:
            continue
        node = ioctl(h, IOCTL_USB_GET_NODE_INFORMATION, b"\0" * 80, 80)
        if not node:
            kernel32.CloseHandle(h)
            continue
        ports = node[6]
        for port in range(1, ports + 1):
            req = struct.pack("<I", port) + b"\0" * (36 - 4 + 32 * 32)
            info = ioctl(h, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, req, len(req))
            if not info or len(info) < 35:
                continue
            status = struct.unpack_from("<I", info, 31)[0]
            if status != 1:  # DeviceConnected
                continue
            dev = info[4:22]
            vid, pid = struct.unpack_from("<HH", dev, 8)
            if vid != VID or pid != PID:
                continue
            found = True
            bcdUSB, cls, sub, proto, mps0 = struct.unpack_from("<HBBBB", dev, 2)
            bcdDevice = struct.unpack_from("<H", dev, 12)[0]
            speed = ["low", "full", "high", "super"][info[23]] if info[23] < 4 else str(info[23])
            print(f"Device {vid:04X}:{pid:04X} on hub port {port}: USB {bcdUSB>>8}.{(bcdUSB>>4)&0xF} {speed}-speed, "
                  f"device class=0x{cls:02X}/0x{sub:02X}/0x{proto:02X}, ep0 maxPacket={mps0}, bcdDevice=0x{bcdDevice:04X}")
            # fetch configuration descriptor (ask for 512 bytes, device returns what it has)
            want = 512
            setup = struct.pack("<IBBHHH", port, 0x80, 0x06, 0x0200, 0, want)
            resp = ioctl(h, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, setup + b"\0" * want, len(setup) + want)
            if not resp or len(resp) <= 12:
                print("  could not read configuration descriptor")
                continue
            cfg = resp[12:]
            total = struct.unpack_from("<H", cfg, 2)[0]
            cfg = cfg[:total]
            print(parse_config(cfg))
            print("raw:", cfg.hex())
        kernel32.CloseHandle(h)
    if not found:
        print(f"No connected device {VID:04X}:{PID:04X} found on any hub (is it plugged in over USB?)")


if __name__ == "__main__":
    main()
