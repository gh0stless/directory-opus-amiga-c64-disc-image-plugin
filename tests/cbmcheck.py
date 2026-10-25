"""Independent consistency check for D64/D71/D81 images (does not use firy).

cbmcheck.py <image>              validate, print summary, exit 1 on errors
cbmcheck.py <image> --dump NAME OUT   write file NAME's content (via the d64 library) to OUT
"""
import sys

def geometry(path, size):
    if size == 174848: return 'd64', 35
    if size == 349696: return 'd71', 70
    if size == 819200: return 'd81', 80
    raise SystemExit(f'unknown image size {size}')

def spt(kind, t):
    if kind == 'd81': return 40
    t2 = t - 35 if t > 35 else t
    return 21 if t2 <= 17 else 19 if t2 <= 24 else 18 if t2 <= 30 else 17

def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    kind, tracks = geometry(path, len(data))
    starts = {}
    o = 0
    for t in range(1, tracks + 1):
        starts[t] = o
        o += spt(kind, t)
    def sec(t, s):
        if not (1 <= t <= tracks) or not (0 <= s < spt(kind, t)):
            return None
        off = (starts[t] + s) * 256
        return data[off:off + 256]

    errors, warnings = [], []
    # --- BAM: free count and bitmap per track (bit set = free)
    bam = {}
    if kind == 'd81':
        for blk, first in ((1, 1), (2, 41)):
            b = sec(40, blk)
            for i in range(40):
                e = b[0x10 + i * 6: 0x10 + i * 6 + 6]
                bam[first + i] = (e[0], int.from_bytes(e[1:6], 'little'))
    else:
        b = sec(18, 0)
        for t in range(1, 36):
            e = b[4 * t: 4 * t + 4]
            bam[t] = (e[0], int.from_bytes(e[1:4], 'little'))
        if kind == 'd71':
            b53 = sec(53, 0)
            for t in range(36, 71):
                i = t - 36
                bam[t] = (b[0xDD + i], int.from_bytes(b53[i * 3: i * 3 + 3], 'little'))
    for t, (count, bits) in bam.items():
        mask = (1 << spt(kind, t)) - 1
        if bin(bits & mask).count('1') != count:
            errors.append(f'BAM track {t}: count {count} != {bin(bits & mask).count("1")} free bits')
    def is_free(t, s):
        return (bam[t][1] >> s) & 1 == 1

    used = {}
    def claim(t, s, owner):
        if (t, s) in used:
            errors.append(f'sector {t}/{s} used by both {used[(t, s)]} and {owner}')
        used[(t, s)] = owner
        if is_free(t, s):
            errors.append(f'sector {t}/{s} ({owner}) is marked free in the BAM')

    # system sectors
    if kind == 'd81':
        for s in (0, 1, 2): claim(40, s, 'header/BAM')
        dirts = (40, 3)
    else:
        claim(18, 0, 'header/BAM')
        if kind == 'd71': claim(53, 0, 'BAM side 2')
        dirts = (18, 1)

    # --- directory
    entries = []
    t, s = dirts
    seen = set()
    while t:
        if (t, s) in seen:
            errors.append('directory chain loop'); break
        seen.add((t, s))
        b = sec(t, s)
        if b is None:
            errors.append(f'directory link to invalid sector {t}/{s}'); break
        claim(t, s, 'directory')
        for i in range(8):
            e = b[i * 32: i * 32 + 32]
            if e[2] == 0:
                continue
            name = e[5:21].rstrip(b'\xa0')
            entries.append((name, e[2], e[3], e[4], int.from_bytes(e[0x1e:0x20], 'little')))
        t, s = b[0], b[1]
        if t == 0 and s != 0xFF and len(seen) and b is not None:
            pass

    # --- files
    names = []
    for name, ftype, ft, fs, blocks in entries:
        label = name.decode('latin-1')
        names.append(label)
        typ = ftype & 0x0F
        if typ not in (1, 2, 3, 4):
            if typ == 0:
                continue  # closed DEL entry (directory art etc.)
            warnings.append(f'{label}: unusual type {ftype:#x}')
            continue
        if not ftype & 0x80:
            warnings.append(f'{label}: file not closed (splat)')
        count = 0
        t, s = ft, fs
        chain = set()
        while t:
            if (t, s) in chain:
                errors.append(f'{label}: chain loop'); break
            chain.add((t, s))
            b = sec(t, s)
            if b is None:
                errors.append(f'{label}: chain points to invalid sector {t}/{s}'); break
            claim(t, s, label)
            count += 1
            if b[0] == 0 and b[1] < 1:
                errors.append(f'{label}: last sector has invalid byte count {b[1]}')
            t, s = b[0], b[1]
        if typ != 4 and count != blocks:
            errors.append(f'{label}: directory says {blocks} blocks, chain has {count}')

    # --- sectors allocated in the BAM that nothing uses (wasted, not corrupt)
    lost = 0
    for t in range(1, tracks + 1):
        for s in range(spt(kind, t)):
            if not is_free(t, s) and (t, s) not in used:
                lost += 1
    if lost:
        warnings.append(f'{lost} sectors allocated but unused')

    dirtrack = 40 if kind == 'd81' else 18
    reserved = {dirtrack} | ({53} if kind == 'd71' else set())
    free = sum(c for t, (c, _) in bam.items() if t not in reserved)
    print(f'{kind} files={len(names)} blocks_free={free} errors={len(errors)} warnings={len(warnings)}')
    for n in names: print('  entry', n)
    for w in warnings: print('  WARN', w)
    for e in errors: print('  ERROR', e)
    return 1 if errors else 0

def dump():
    from d64 import DiskImage
    path, name, out = sys.argv[1], sys.argv[3], sys.argv[4]
    with DiskImage(path) as img:
        with img.path(name.encode('latin-1')).open() as f:
            open(out, 'wb').write(f.read())

if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[2] == '--dump':
        dump()
    else:
        sys.exit(main())
