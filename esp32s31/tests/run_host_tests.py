"""Build and run protocol tests against the actual ported C++ modules."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import os
import re

root = Path(__file__).resolve().parents[1]
def report_bytes(path, name):
    source = path.read_text(encoding="utf-8")
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    body = re.search(r"\b" + name + r"\[\]\s*=\s*\{(.*?)\};", source, re.S).group(1)
    return bytes(int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body))
upstream = root.parent / "src/usb_descriptors.cpp"
if upstream.is_file():
    for name in ("desc_hid_report_ds", "desc_hid_report_dse", "desc_hid_report_kbd"):
        assert report_bytes(upstream, name) == report_bytes(root / "main/upstream/usb_descriptors.cpp", name), name
    print("PASS: DualSense, Edge and keyboard HID report descriptors match upstream byte for byte", flush=True)
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required for the host protocol tests")
os.environ["PATH"] = str(Path(compiler).parent) + os.pathsep + os.environ["PATH"]
with tempfile.TemporaryDirectory(prefix="ds5-s31-tests-") as folder:
  for name in ("protocol_test", "config_test", "wake_test"):
    binary = Path(folder) / (name + ".exe")
    subprocess.run([
        compiler, "-std=c++20", "-O1", "-DENABLE_WAKE_HID", "-DENABLE_SERIAL=0",
        "-DCFG_TUSB_MCU=OPT_MCU_NONE", '-DCFG_TUSB_CONFIG_FILE="tusb_config.h"',
        "-I" + str(root / "tests/host"),
        "-I" + str(root / "main/upstream"),
        "-I" + str(root / "managed_components/espressif__tinyusb/src"),
        str(root / "tests" / (name + ".cpp")), "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)
