#!/usr/bin/env python3
"""
Find function boundaries in overlay 36
"""
import struct

with open('/home/null/hg-engine/base/overlay/overlay_0036.bin', 'rb') as f:
    data = f.read()

OVERLAY_LOAD = 0x021E5900

print(f'Overlay size: {len(data)} bytes')
print()

# Find all PUSH {lr} and POP {lr} / POP {r4-r6, lr}
print('Function prologues (PUSH) and epilogues (POP):')
for i in range(0, len(data) - 2, 2):
    val = struct.unpack_from('<H', data, i)[0]
    if val == 0xB500:
        print(f'  {i:04X}: PUSH {{lr}}  -> function start at {OVERLAY_LOAD+i:08X}')
    elif val == 0xB570:
        print(f'  {i:04X}: PUSH {{r4-r6, lr}}  -> function start at {OVERLAY_LOAD+i:08X}')
    elif val == 0xBD00:
        print(f'  {i:04X}: POP {{pc}}  -> function end at {OVERLAY_LOAD+i:08X}')
    elif val == 0xBD70:
        print(f'  {i:04X}: POP {{r4-r6, pc}}  -> function end at {OVERLAY_LOAD+i:08X}')

print()
print('Searching for RegisterMainOverlay call pattern (LDR r1, [pc] followed by BL):')
for i in range(0, len(data) - 8, 2):
    # Look for LDR r1, [pc, #offset] pattern (0x4900-0x49FF)
    val = struct.unpack_from('<H', data, i)[0]
    if 0x4900 <= val <= 0x49FF:
        # Check if followed by BL
        if i+4 < len(data) - 4:
            val1 = struct.unpack_from('<H', data, i+2)[0]
            val2 = struct.unpack_from('<H', data, i+4)[0]
            if 0xF000 <= val1 <= 0xF7FF and 0xF800 <= val2 <= 0xFFFF:
                # This might be loading OakSpeech template and calling RegisterMainOverlay
                print(f'  {i:04X}: LDR r1, [pc] + BL sequence')
                ctx = data[max(0,i-4):i+12]
                print(f'    Context: {ctx.hex()}')
                print()
