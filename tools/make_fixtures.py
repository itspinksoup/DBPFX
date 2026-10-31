#!/usr/bin/env python3
"""Independent reference implementation used to cross-check DBPFX.

Writes DBPF packages in the shapes The Sims 3 / Medieval (DBPF 2.0, RefPack) and The Sims 4
(DBPF 2.1, zlib, deleted-record markers) use, plus a JSON manifest of the expected uncompressed
contents. Also provides a RefPack decoder that is
used to validate the C++ RefPack *encoder* (see check_package()).

usage: make_fixtures.py <out-dir> [--big N]
       make_fixtures.py --check <file.package> <manifest.json>
"""
import hashlib
import json
import os
import random
import struct
import sys
import zlib

# ----------------------------------------------------------------------------- RefPack
def refpack_decompress(d: bytes) -> bytes:
    flags = d[0]
    assert d[1] == 0xFB, "bad RefPack signature"
    w = 4 if flags & 0x80 else 3
    pos = 2 + (w if flags & 0x01 else 0)
    size = int.from_bytes(d[pos:pos + w], "big")
    pos += w
    out = bytearray()
    while len(out) < size:
        b0 = d[pos]
        if b0 < 0x80:
            b1 = d[pos + 1]; pos += 2
            plain, n, off = b0 & 3, ((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = d[pos + 1], d[pos + 2]; pos += 3
            plain, n, off = (b1 >> 6) & 3, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = d[pos + 1], d[pos + 2], d[pos + 3]; pos += 4
            plain, n, off = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            pos += 1
            k = ((b0 & 0x1F) << 2) + 4
            out += d[pos:pos + k]; pos += k
            continue
        else:
            pos += 1
            k = b0 & 3
            out += d[pos:pos + k]; pos += k
            break
        out += d[pos:pos + plain]; pos += plain
        start = len(out) - off
        assert start >= 0, "back-reference before start"
        for i in range(n):
            out.append(out[start + i])
    assert len(out) == size, f"RefPack produced {len(out)} bytes, header says {size}"
    return bytes(out)


def refpack_compress_simple(data: bytes) -> bytes:
    """Deliberately different from the C++ encoder: greedy, 2/3-byte commands only."""
    n = len(data)
    out = bytearray([0x10, 0xFB]) + n.to_bytes(3, "big")
    table = {}
    i = lit = 0

    def flush(upto, keep):
        nonlocal lit
        while upto - lit > keep:
            k = min((upto - lit) & ~3, 112)
            if k < 4:
                break
            out.append(0xE0 + ((k - 4) >> 2)); out.extend(data[lit:lit + k]); lit += k

    while i + 3 <= n:
        key = data[i:i + 3]
        cand = table.get(key)
        table[key] = i
        if cand is not None and i - cand <= 16384:
            off = i - cand
            L = 3
            while i + L < n and L < 67 and data[cand + L] == data[i + L]:
                L += 1
            if L >= 4 or off <= 1024:
                flush(i, 3)
                plain = i - lit
                o = off - 1
                if L <= 10 and off <= 1024:
                    out += bytes([((o >> 3) & 0x60) | ((L - 3) << 2) | plain, o & 0xFF])
                else:
                    out += bytes([0x80 | (L - 4), (plain << 6) | (o >> 8), o & 0xFF])
                out.extend(data[lit:i]); i += L; lit = i
                continue
        i += 1
    flush(n, 3)
    out.append(0xFC + (n - lit)); out.extend(data[lit:n])
    assert refpack_decompress(bytes(out)) == data
    return bytes(out)

# ----------------------------------------------------------------------------- resources
def stbl_ts3(strings):
    b = bytearray(b"STBL") + struct.pack("<BHIHI", 2, 0, len(strings), 0, 0)
    for key, text in strings:
        u = text.encode("utf-16-le")
        b += struct.pack("<QI", key, len(u) // 2) + u
    return bytes(b)


def stbl_ts4(strings):
    body = bytearray()
    total = 0
    for key, text in strings:
        u = text.encode("utf-8")
        body += struct.pack("<IBH", key, 0, len(u)) + u
        total += len(u) + 1
    return b"STBL" + struct.pack("<HBQ2sI", 5, 0, len(strings), b"\0\0", total) + bytes(body)


def name_map(names):
    b = bytearray(struct.pack("<II", 1, len(names)))
    for inst, name in names:
        u = name.encode("utf-8")
        b += struct.pack("<QI", inst, len(u)) + u
    return bytes(b)

# ----------------------------------------------------------------------------- DBPF writer
def write_package(path, game, resources, flags_override=None, deleted=()):
    """resources: list of (type, group, instance, data, mode) with mode in raw|refpack|zlib."""
    minor = 1 if game == "sims4" else 0
    body = bytearray(b"\0" * 96)
    entries = []
    manifest = []
    for (t, g, inst, data, mode) in resources:
        off = len(body)
        if mode == "refpack":
            stored, comp = refpack_compress_simple(data), 0xFFFF
        elif mode == "zlib":
            stored, comp = zlib.compress(data, 6), 0x5A42
        else:
            stored, comp = data, 0
        body += stored
        entries.append((t, g, inst, off, len(stored), len(data), comp))
        manifest.append({"key": f"{t:08X}-{g:08X}-{inst:016X}", "size": len(data),
                         "sha256": hashlib.sha256(data).hexdigest(), "mode": mode})
    for (t, g, inst) in deleted:  # TS4 deleted-record markers
        entries.append((t, g, inst, len(body), 1, 0xFFFFFFFF, 0xFFE0))
        body += b"\0"
    types = {e[0] for e in entries}; groups = {e[1] for e in entries}; ihis = {e[2] >> 32 for e in entries}
    flags = flags_override if flags_override is not None else (
        (1 if len(types) <= 1 else 0) | (2 if len(groups) <= 1 else 0) | (4 if len(ihis) <= 1 else 0))
    idx = bytearray(struct.pack("<I", flags))
    if flags & 1: idx += struct.pack("<I", entries[0][0])
    if flags & 2: idx += struct.pack("<I", entries[0][1])
    if flags & 4: idx += struct.pack("<I", entries[0][2] >> 32)
    for (t, g, inst, off, fs, ms, comp) in entries:
        if not flags & 1: idx += struct.pack("<I", t)
        if not flags & 2: idx += struct.pack("<I", g)
        if not flags & 4: idx += struct.pack("<I", inst >> 32)
        idx += struct.pack("<IIIIHH", inst & 0xFFFFFFFF, off, fs | 0x80000000, ms, comp, 1)
    index_pos = len(body)
    body += idx
    hdr = bytearray(96)
    hdr[0:4] = b"DBPF"
    struct.pack_into("<II", hdr, 4, 2, minor)
    struct.pack_into("<I", hdr, 0x24, len(entries))
    struct.pack_into("<I", hdr, 0x2C, len(idx))
    struct.pack_into("<I", hdr, 0x3C, 3)
    struct.pack_into("<I", hdr, 0x40, index_pos)
    body[0:96] = hdr
    with open(path, "wb") as f:
        f.write(body)
    return manifest

# ----------------------------------------------------------------------------- reader (for --check)
def read_package(path):
    d = open(path, "rb").read()
    assert d[:4] == b"DBPF"
    major, minor = struct.unpack_from("<II", d, 4)
    count, = struct.unpack_from("<I", d, 0x24)
    pos, = struct.unpack_from("<I", d, 0x40)
    if pos == 0:
        pos, = struct.unpack_from("<I", d, 0x28)
    flags, = struct.unpack_from("<I", d, pos); p = pos + 4
    const = {}
    for bit, name in ((1, "t"), (2, "g"), (4, "ih")):
        if flags & bit:
            const[name], = struct.unpack_from("<I", d, p); p += 4
    out = {}
    for _ in range(count):
        vals = {}
        for bit, name in ((1, "t"), (2, "g"), (4, "ih")):
            if flags & bit:
                vals[name] = const[name]
            else:
                vals[name], = struct.unpack_from("<I", d, p); p += 4
        il, off, fs, ms, comp, _c = struct.unpack_from("<IIIIHH", d, p); p += 20
        fs &= 0x7FFFFFFF
        key = f"{vals['t']:08X}-{vals['g']:08X}-{(vals['ih'] << 32) | il:016X}"
        if comp == 0xFFE0:
            continue
        raw = d[off:off + fs]
        if fs == ms:
            data = raw
        elif raw[1] == 0xFB:
            data = refpack_decompress(raw)
        else:
            data = zlib.decompress(raw)
        assert len(data) == ms, key
        out[key] = (data, comp)
    return (major, minor), out


def check_package(path, manifest_path, require_comp=None):
    (major, minor), got = read_package(path)
    manifest = json.load(open(manifest_path))
    bad = 0
    for m in manifest["resources"]:
        if m["key"] not in got:
            print("missing", m["key"]); bad += 1; continue
        data, comp = got[m["key"]]
        if hashlib.sha256(data).hexdigest() != m["sha256"]:
            print("content mismatch", m["key"]); bad += 1
        if require_comp is not None and comp not in require_comp:
            print("unexpected compression", hex(comp), m["key"]); bad += 1
    n_comp = sum(1 for (_, c) in got.values() if c not in (0,))
    print(f"python-check {os.path.basename(path)}: DBPF {major}.{minor}, {len(got)} resources, "
          f"{n_comp} compressed, {bad} problems")
    return bad == 0

# ----------------------------------------------------------------------------- fixtures
def make(outdir, big):
    os.makedirs(outdir, exist_ok=True)
    rnd = random.Random(1234)
    words = "sim lot plumbob career skill moodlet woohoo grilled cheese llama simoleon build buy create".split()

    def text(n):
        return (" ".join(rnd.choice(words) for _ in range(n))).encode()

    # ---- The Sims 3 style: RefPack (0xFFFF), mixed types/groups, UTF-16 STBL
    ts3 = [
        (0x220557DA, 0, 0x0017A2C3F2E55B21, stbl_ts3([(0xE52A9C1EC7E4B6D1, "Plumbob"),
                                                       (0x00000000DEADBEEF, "Ñandú – ünïcødé ✓"),
                                                       (0x1234, "")]), "refpack"),
        (0x0166038C, 0, 0x0000000000001000, name_map([(0x1, "S3_Object"), (0xABCDEF0123, "llama_hat")]), "raw"),
        (0x073FAA07, 0, 0x5C9A1E73B2D4F001, text(4000), "refpack"),
        (0x00B2D882, 0x00000001, 0x00000000000000AA, bytes(rnd.getrandbits(8) for _ in range(3000)), "raw"),
        (0x0333406C, 0x00000001, 0x8000000000000001, text(1500), "refpack"),
        (0x0333406C, 0x00000002, 0x8000000000000002, b"", "raw"),
    ]
    m3 = write_package(os.path.join(outdir, "ts3.package"), "sims3", ts3)
    json.dump({"game": "sims3", "resources": m3}, open(os.path.join(outdir, "ts3.json"), "w"), indent=1)

    # ---- The Sims 4 style: zlib (0x5A42) + one RefPack, const group, UTF-8 STBL, deleted record
    ts4 = [
        (0x220557DA, 0x80000000, 0x00F7A2C3F2E55B21, stbl_ts4([(0x2A9C1EC7, "Plumbob"), (0xDEADBEEF, "Ñandú ✓"), (1, "")]), "zlib"),
        (0x545AC67A, 0x80000000, 0x00F7A2C3F2E55B22, text(3000), "zlib"),
        (0x62E94D38, 0x80000000, 0x00F7A2C3F2E55B23, text(800), "refpack"),
        (0x00B2D882, 0x80000000, 0x00F7A2C3F2E55B24, bytes(rnd.getrandbits(8) for _ in range(2000)), "raw"),
    ]
    m4 = write_package(os.path.join(outdir, "ts4.package"), "sims4", ts4, deleted=[(0x545AC67A, 0x80000000, 0x00F7A2C3F2E55B99)])
    json.dump({"game": "sims4", "resources": m4}, open(os.path.join(outdir, "ts4.json"), "w"), indent=1)

    # ---- all-constant index (single resource), and an empty package
    m1 = write_package(os.path.join(outdir, "single.package"), "sims3", [(0x0166038C, 7, 0x42, name_map([(1, "x")]), "raw")])
    json.dump({"game": "sims3", "resources": m1}, open(os.path.join(outdir, "single.json"), "w"), indent=1)
    hdr = bytearray(96); hdr[0:4] = b"DBPF"; struct.pack_into("<IIIII", hdr, 4, 2, 0, 0, 0, 0)
    struct.pack_into("<I", hdr, 0x3C, 3); struct.pack_into("<I", hdr, 0x2C, 4); struct.pack_into("<I", hdr, 0x40, 96)
    open(os.path.join(outdir, "empty.package"), "wb").write(bytes(hdr) + b"\0\0\0\0")

    # ---- big package for the benchmark (zlib, like an EA full-build package)
    if big:
        res = []
        for i in range(big):
            size = rnd.choice((300, 1200, 4000, 16000))
            blob = (text(size // 6) + bytes(rnd.getrandbits(8) for _ in range(size // 50)))[:size]
            res.append((rnd.choice((0x034AEECB, 0x00B2D882, 0x015A1849, 0x0333406C, 0x220557DA)),
                        rnd.randrange(0, 4), rnd.getrandbits(64), blob, "zlib" if i % 4 else "raw"))
        mb = write_package(os.path.join(outdir, "big.package"), "sims4", res)
        json.dump({"game": "sims4", "resources": mb}, open(os.path.join(outdir, "big.json"), "w"))
    print("fixtures written to", outdir)


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "--check":
        req = {0} | {int(x, 16) for x in sys.argv[4].split(",")} if len(sys.argv) > 4 else None
        sys.exit(0 if check_package(sys.argv[2], sys.argv[3], req) else 1)
    big = int(sys.argv[sys.argv.index("--big") + 1]) if "--big" in sys.argv else 0
    make(sys.argv[1], big)
