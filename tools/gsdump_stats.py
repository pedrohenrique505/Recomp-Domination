#!/usr/bin/env python3
"""Estatísticas de um GS dump (formato PCSX2, .gs ou .gs.zst) por frame: pacotes, uploads de imagem,
primitivas por tipo/flags e registradores escritos. Serve para comparar o fluxo do runtime com o do PCSX2.
Uso: python3 -I tools/gsdump_stats.py dump.gs[.zst] [frames]"""
import collections
import struct
import subprocess
import sys

PRIM = ['POINT', 'LINE', 'LSTRIP', 'TRI', 'TSTRIP', 'TFAN', 'SPRITE', '?']


def load(path):
    if path.endswith('.zst'):
        return subprocess.run(['zstd', '-dc', path], capture_output=True, check=True).stdout
    return open(path, 'rb').read()


def frames(b):
    hs, = struct.unpack_from('<I', b, 4)
    ssz, = struct.unpack_from('<I', b, 12)
    p = 8 + hs + ssz + 0x2000
    cur = []
    while p < len(b):
        t = b[p]
        p += 1
        if t == 0:
            sz, = struct.unpack_from('<I', b, p + 1)
            cur.append(b[p + 5:p + 5 + sz])
            p += 5 + sz
        elif t == 1:
            p += 1
            yield cur
            cur = []
        elif t == 2:
            p += 4
        elif t == 3:
            p += 0x2000
        else:
            raise ValueError(f'pacote desconhecido {t} em {p}')


def stats(packets):
    st = collections.Counter()
    regs = collections.Counter()
    for pkt in packets:
        q = 0
        while q + 16 <= len(pkt):
            lo, hi = struct.unpack_from('<QQ', pkt, q)
            nloop, eop, pre = lo & 0x7FFF, (lo >> 15) & 1, (lo >> 46) & 1
            prim, flg, nreg = (lo >> 47) & 0x7FF, (lo >> 58) & 3, ((lo >> 60) & 0xF) or 16
            q += 16
            if pre:
                st[f'prim {PRIM[prim & 7]}{" TME" if prim & 16 else ""}{" FGE" if prim & 32 else ""}{" ABE" if prim & 64 else ""}'] += 1
            if flg >= 2:
                st['IMAGE qw'] += nloop
                q += nloop * 16
            elif flg == 0:
                rl = [(hi >> (4 * i)) & 0xF for i in range(nreg)]
                for i in range(nloop * nreg):
                    r = rl[i % nreg]
                    if r == 0xE:
                        v, a = struct.unpack_from('<QQ', pkt, q + i * 16)
                        a &= 0xFF
                        regs[a] += 1
                        if a == 0x00:
                            st[f'prim {PRIM[v & 7]}{" TME" if v & 16 else ""}{" FGE" if v & 32 else ""}{" ABE" if v & 64 else ""}'] += 1
                    elif r in (4, 5, 0xC, 0xD):
                        st['vertices'] += 1
                q += nloop * nreg * 16
            else:
                q += ((nloop * nreg + 1) // 2) * 16
                st['vertices(reglist)'] += nloop
            if eop and q >= len(pkt):
                break
    return st, regs


REGNAMES = {0x06: 'TEX0_1', 0x07: 'TEX0_2', 0x14: 'TEX1_1', 0x42: 'ALPHA_1', 0x47: 'TEST_1', 0x4C: 'FRAME_1',
            0x4E: 'ZBUF_1', 0x50: 'BITBLTBUF', 0x53: 'TRXDIR', 0x3D: 'FOGCOL', 0x00: 'PRIM', 0x08: 'CLAMP_1',
            0x1C: 'TEXCLUT', 0x3F: 'TEXFLUSH', 0x40: 'SCISSOR_1', 0x18: 'XYOFFSET_1', 0x3B: 'TEXA'}

if __name__ == '__main__':
    data = load(sys.argv[1])
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    for i, fr in enumerate(frames(data)):
        if i >= limit:
            break
        st, regs = stats(fr)
        print(f'== frame {i}: {len(fr)} pacotes, {sum(map(len, fr)) / 1024:.0f} KB')
        for k, v in sorted(st.items(), key=lambda kv: -kv[1]):
            print(f'   {v:7d}  {k}')
        print('   regs A+D:', ', '.join(f'{REGNAMES.get(r, hex(r))}={c}' for r, c in regs.most_common(14)))
