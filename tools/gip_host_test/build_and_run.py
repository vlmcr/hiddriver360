"""Extract the GIP section of hiddriver/main.cpp and build/run the host test.

The driver assumes the console's 32 bit layout (pointers fit in a DWORD, the
control TRB sits 36 bytes into the device extension), so the harness is built
as 32 bit x86 with MSVC when Visual Studio Build Tools are installed. g++ /
clang++ are used as a 64 bit fallback; the pointer-size dependent init checks
are reported but expected to fail there.

Usage: python build_and_run.py
"""
import glob
import io
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", ".."))
main_cpp = os.path.join(root, "hiddriver", "main.cpp")

src = io.open(main_cpp, encoding="utf-8", errors="replace").read()
start_marker = "// ---- GIP (Xbox One / Xbox Series controllers) specific start ----"
end_marker = "// ---- GIP specific end ----"
section = src[src.index(start_marker):src.index(end_marker)]
with io.open(os.path.join(here, "gip_section.inc"), "w", encoding="utf-8", newline="\n") as f:
    f.write("// generated from hiddriver/main.cpp by build_and_run.py, do not edit\n")
    f.write(section)

test_cpp = os.path.join(here, "gip_host_test.cpp")
exe = os.path.join(here, "gip_host_test.exe")


def find_vcvars32():
    pattern = r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars32.bat"
    hits = glob.glob(pattern)
    return hits[0] if hits else None


vcvars = find_vcvars32()
if vcvars:
    print("building 32 bit with MSVC:", vcvars)
    cmd = 'call "%s" >nul && cl /nologo /EHsc /W3 /Fe"%s" "%s"' % (vcvars, exe, test_cpp)
    subprocess.check_call(cmd, shell=True, cwd=here)
else:
    cxx = shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        sys.exit("no MSVC, g++ or clang++ found")
    print("building 64 bit with", cxx, "(init path offset checks will not be meaningful)")
    cmd = [cxx, "-std=c++11", "-Wall", "-Wno-unused-function", "-Wno-sign-compare", "-o", exe, test_cpp]
    subprocess.check_call(cmd)

sys.exit(subprocess.call([exe]))
