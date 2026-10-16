#!/usr/bin/env python3
"""Lista as primeiras strips (PACKED com PRE) de um frame de GS dump: NLOOP, regs e os primeiros vértices
(XYZF2/XYZ2 com o bit ADC, ST e RGBAQ). Uso: python3 -I tools/gsdump_strips.py dump [frame] [n]"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from gsdump_stats import frames, load  # noqa: E402

data = load(sys.argv[1])
want = int(sys.argv[2]) if len(sys.argv) > 2 else 1
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 8
for i, fr in enumerate(frames(data)):
    if i != want:
        continue
    shown = 0
    lens = []
    for pkt in fr:
        q = 0
        while q + 16 <= len(pkt):
            lo, hi = struct.unpack_from('<QQ', pkt, q)
            nloop, eop, pre, prim = lo & 0x7FFF, (lo >> 15) & 1, (lo >> 46) & 1, (lo >> 47) & 0x7FF
            flg, nreg = (lo >> 58) & 3, ((lo >> 60) & 0xF) or 16
            q += 16
            regs = [(hi >> (4 * k)) & 0xF for k in range(nreg)]
            if flg == 0 and pre:
                lens.append(nloop)
                if shown < limit:
                    shown += 1
                    adc = 0
                    print(f'strip prim={prim:#x} nloop={nloop} regs={regs}')
                    for v in range(min(nloop, 4)):
                        line = []
                        for k, r in enumerate(regs):
                            a, b, c, d = struct.unpack_from('<IIII', pkt, q + (v * nreg + k) * 16)
                            if r == 2:
                                line.append(f'ST=({struct.unpack("<f", struct.pack("<I", a))[0]:.3f},{struct.unpack("<f", struct.pack("<I", b))[0]:.3f})')
                            elif r == 1:
                                line.append(f'RGBA=({a & 255},{b & 255},{c & 255},{d & 255})')
                            elif r in (4, 5):
                                line.append(f'XYZ=({(a & 0xFFFF) / 16 - 2048:.1f},{(b & 0xFFFF) / 16 - 2048:.1f}) z={c >> 4 if r == 4 else c} f={d >> 4 & 0xFF if r == 4 else "-"} ADC={(d >> 15) & 1}')
                        print('    ', ' '.join(line))
                    for v in range(nloop):
                        k = regs.index(4) if 4 in regs else (regs.index(5) if 5 in regs else None)
                        if k is not None:
                            d, = struct.unpack_from('<I', pkt, q + (v * nreg + k) * 16 + 12)
                            adc += (d >> 15) & 1
                    print(f'     ADC=1 em {adc}/{nloop} vértices')
            if flg == 0:
                q += nloop * nreg * 16
            elif flg == 1:
                q += ((nloop * nreg + 1) // 2) * 16
            else:
                q += nloop * 16
            if eop and q >= len(pkt):
                break
    print(f'strips: {len(lens)}  média nloop: {sum(lens) / max(1, len(lens)):.1f}  máx: {max(lens or [0])}')
