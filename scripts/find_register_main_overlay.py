#!/usr/bin/env python3
"""
Find RegisterMainOverlay in ARM9 binary.
"""

import struct

ARM9_PATH = '/home/null/hg-engine/base/arm9.bin'

with open(ARM9_PATH, 'rb') as f:
    data = f.read()

print(f"ARM9 size: {len(data)} bytes")

# Looking for patterns related to overlay loading
# RegisterMainOverlay is called with overlay ID and a pointer to overlay template
# In THUMB, this would involve pushing args and calling a function

# Common pattern for RegisterMainOverlay in pret:
# LDR r0, =FS_OVERLAY_ID_NONE  (or specific overlay ID)
# LDR r1, =overlayTemplate
# BL RegisterMainOverlay

# Let's look for the overlay 53 reference (0x35) near function calls
for i in range(0, len(data) - 20, 2):
    # Check for MOV rX, #0x35 pattern (overlay 53)
    val = struct.unpack_from('<H', data, i)[0]
    # MOVS rX, #imm8: 0x20XX where XX is imm8
    if val == 0x2035:  # MOVS r0, #53
        print(f"Found MOVS r0, #53 at ARM9 offset {i:06X} (addr {0x02000000+i:08X})")
        # Print surrounding context
        context = data[max(0,i-20):i+20]
        print(f"  Context: {context.hex()}")

# Also look for MOVS r0, #0xFF (FS_OVERLAY_ID_NONE = -1)
for i in range(0, len(data) - 20, 2):
    val = struct.unpack_from('<H', data, i)[0]
    if val == 0x20FF:  # MOVS r0, #255 (which is -1 in signed 8-bit)
        # Check if nearby there's a reference to overlay 53 template
        context = data[max(0,i-10):i+30]
        if b'\x35' in context or b'\x00\x00' in context:
            print(f"Found MOVS r0, #255 at ARM9 offset {i:06X} (addr {0x02000000+i:08X})")
            print(f"  Context: {context.hex()}")
