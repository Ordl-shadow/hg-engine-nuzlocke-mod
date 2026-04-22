#!/usr/bin/env python3
"""
Search for overlay 36 template in the overlay binary
"""
import struct

with open('/home/null/hg-engine/base/overlay/overlay_0036.bin', 'rb') as f:
    data = f.read()

OVERLAY_LOAD = 0x021E5900
OVERLAY_END = OVERLAY_LOAD + len(data)

# Relaxed search: just look for 3 consecutive thumb addresses
print('Searching for 3 consecutive thumb function pointers...')
for i in range(0, len(data) - 12, 4):
    words = struct.unpack_from('<III', data, i)
    
    valid = True
    for w in words:
        if (w & 1) != 1:
            valid = False
            break
        addr = w & ~1
        if not (OVERLAY_LOAD <= addr < OVERLAY_END):
            valid = False
            break
    
    if valid:
        print(f'Found 3 thumb pointers at overlay offset {i:04X}:')
        print(f'  Ptr1={words[0]:08X} (offset {(words[0]&~1) - OVERLAY_LOAD:04X})')
        print(f'  Ptr2={words[1]:08X} (offset {(words[1]&~1) - OVERLAY_LOAD:04X})')
        print(f'  Ptr3={words[2]:08X} (offset {(words[2]&~1) - OVERLAY_LOAD:04X})')
        # Show next word (might be ovy_id)
        if i + 12 < len(data):
            next_word = struct.unpack_from('<I', data, i+12)[0]
            print(f'  Next word: {next_word:08X}')
        print()

# Also search for RegisterMainOverlay call pattern
# In THUMB, this would be a BL instruction
print('Searching for BL instructions...')
for i in range(0, len(data) - 4, 2):
    val1 = struct.unpack_from('<H', data, i)[0]
    if 0xF000 <= val1 <= 0xF7FF and i+3 < len(data):
        val2 = struct.unpack_from('<H', data, i+2)[0]
        if 0xF800 <= val2 <= 0xFFFF:
            # Decode BL target (approximate)
            s = (val1 >> 10) & 1
            j1 = 1 - ((val2 >> 13) & 1)
            j2 = 1 - ((val2 >> 11) & 1)
            imm10 = val1 & 0x3FF
            imm11 = val2 & 0x7FF
            imm = (s << 24) | (j1 << 23) | (j2 << 22) | (imm10 << 12) | (imm11 << 1)
            if imm & 0x800000:
                imm -= 0x1000000
            target = (OVERLAY_LOAD + i + 4) + imm
            print(f'  Offset {i:04X}: BL to ~{target:08X}')
