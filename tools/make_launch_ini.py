"""Set plugin2 = Hdd:\\xbdm.xex in a copy of the console's launch.ini (bytes preserved otherwise)."""
import io
import sys

src, dst = sys.argv[1], sys.argv[2]
s = io.open(src, "rb").read()
key = b"plugin2 = "
i = s.find(key)
assert i >= 0, "plugin2 line not found"
eol = s.find(b"\n", i)
line = s[i:eol].rstrip(b"\r")
assert line == key, "plugin2 already set: %r" % line
value = b"Hdd:" + bytes([0x5C]) + b"xbdm.xex"   # backslash spelled out to survive any escaping
new = s[:i] + key + value + s[i + len(line):]
io.open(dst, "wb").write(new)
print("ok:", (key + value).decode())
