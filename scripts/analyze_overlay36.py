#!/usr/bin/env python3
"""
Analyze overlay 36 to find the RegisterMainOverlay call site.
Overlay 36 (NewGame) and 53 (OakSpeech) both load at 021E5900.
"""

import struct
import subprocess

OVERLAY_36_PATH = '/home/null/hg-engine/base/overlay/overlay_0036.bin'
OVERLAY_36_LOAD = 0x021E5900

with open(OVERLAY_36_PATH, 'rb') as f:
    data = f.read()

print(f"Overlay 36 size: {len(data)} bytes")
print(f"Load address: {OVERLAY_36_LOAD:08X}")

# Try to disassemble using objdump with THUMB mode
try:
    result = subprocess.run(
        ['arm-none-eabi-objdump', '-b', 'binary', '-m', 'armv5t', '-M', 'force-thumb', '-D', OVERLAY_36_PATH],
        capture_output=True, text=True
    )
    print("\n--- THUMB Disassembly ---")
    print(result.stdout[:4000])
except Exception as e:
    print(f"Disassembly failed: {e}")
