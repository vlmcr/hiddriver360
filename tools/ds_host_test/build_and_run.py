"""Extract the DualSense section of hiddriver/main.cpp and build/run the host test (32 bit MSVC, g++ fallback)."""
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
section = src[src.index("// ---- DualSense specific start ----"):src.index("// ---- DualSense specific end ----")]
with io.open(os.path.join(here, "ds_section.inc"), "w", encoding="utf-8", newline="\n") as f:
    f.write("// generated from hiddriver/main.cpp by build_and_run.py, do not edit\n")
    f.write(section)

test_cpp = os.path.join(here, "ds_host_test.cpp")
exe = os.path.join(here, "ds_host_test.exe")
hits = glob.glob(r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars32.bat")
if hits:
    print("building 32 bit with MSVC")
    subprocess.check_call('call "%s" >nul && cl /nologo /EHsc /W3 /Fe"%s" "%s"' % (hits[0], exe, test_cpp), shell=True, cwd=here)
else:
    cxx = shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        sys.exit("no compiler found")
    subprocess.check_call([cxx, "-std=c++11", "-Wall", "-Wno-unused-function", "-Wno-sign-compare", "-o", exe, test_cpp])
sys.exit(subprocess.call([exe]))
