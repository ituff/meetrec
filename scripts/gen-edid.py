"""Generates the 128-byte EDID base block for the Virtual Recording Display.

Modes reported by the driver (hard-coded in the parse callback, consistent
with this EDID):
    1920x1080@30 (preferred), 1920x1080@60, 1280x720@30

DTD1 = 1920x1080@30 (CEA-861 1080p30: 74.25 MHz, htotal 2200, vtotal 1125)
DTD2 = 1920x1080@60 (CEA-861 1080p60: 148.5 MHz, same totals)
Descriptors 3+4 carry the two-part monitor name "Virtual Recording Display"
(EDID name descriptors concatenate across consecutive 0xFC blocks).
"""


def cea1080(pclock_khz):
    ha, hb = 1920, 280
    va, vb = 1080, 45
    hfp, hsw = 88, 44
    vfp, vsw = 4, 5
    mm_w, mm_h = 530, 298
    d = bytearray(18)
    d[0] = pclock_khz & 0xFF
    d[1] = pclock_khz >> 8
    d[2] = ha & 0xFF
    d[3] = hb & 0xFF
    d[4] = ((ha >> 8) << 4) | (hb >> 8)
    d[5] = va & 0xFF
    d[6] = vb & 0xFF
    d[7] = ((va >> 8) << 4) | (vb >> 8)
    d[8] = (hfp & 0x0F) << 4 | (hsw & 0x0F)
    d[9] = (vfp & 0x0F) << 4 | (vsw & 0x0F)
    d[10] = (hfp >> 4) << 4 | (hsw >> 4)
    d[11] = (vfp >> 4) << 4 | (vsw >> 4)
    d[12] = mm_w & 0xFF
    d[13] = mm_h & 0xFF
    d[14] = (mm_w >> 8) << 4 | (mm_h >> 8)
    d[15] = 0
    d[16] = 0
    d[17] = 0x1E
    return d


def name_descriptor(text, terminated):
    assert 1 <= len(text) <= 13
    assert (not terminated and len(text) == 13) or (terminated and len(text) <= 12)
    d = bytearray(18)
    d[3] = 0xFC
    b = text.encode('ascii')
    d[5:5 + len(b)] = b
    if terminated:
        d[5 + len(b)] = 0x0A
    return d


def build():
    e = bytearray(128)
    e[0:8] = bytes([0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00])
    mfg = (22 << 11) | (19 << 6) | (18 << 1)  # "VSR"
    e[8] = mfg >> 8
    e[9] = mfg & 0xFF
    e[10:12] = (0x0001).to_bytes(2, 'little')
    e[12:16] = (0x20260909).to_bytes(4, 'little')
    e[16] = 36
    e[17] = 2026 - 1990
    e[18] = 1
    e[19] = 4
    e[20] = 0x80
    e[21] = 53
    e[22] = 30
    e[23] = 120
    e[24] = 0x80
    e[25:35] = bytes([0xBB, 0xC4, 0xA3, 0x4C, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54])
    e[35:38] = b'\x00\x00\x00'
    for i in range(38, 54):
        e[i] = 0x01
    e[54:72] = cea1080(7425)
    e[72:90] = cea1080(14850)
    e[90:108] = name_descriptor("Virtual Reco", True)
    e[108:126] = name_descriptor("rding Display", False)
    e[127] = (-sum(e[0:127])) & 0xFF
    assert sum(e) % 256 == 0
    return bytes(e)


edid = build()
tokens = [f"0x{b:02X}" for b in edid]
lines = []
for i in range(0, len(tokens), 12):
    lines.append("    " + ", ".join(tokens[i:i + 12]) + ",")
print("\n".join(lines))
print("// checksum:", hex(edid[127]), "sum%256 =", sum(edid) % 256)
