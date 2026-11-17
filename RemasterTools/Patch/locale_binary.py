"""TRB locale/subtitle readers and writers from the remaster locale_tool."""
import struct
ARM_NOP = bytes.fromhex("0000a0e1")

def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def i32(b, o):
    return struct.unpack_from('<i', b, o)[0]


def align(n, a):
    return (n + a - 1) // a * a


def wstr_at(buf, off):
    """NUL terminated UTF-16LE string (lone surrogates survive the round trip)."""
    e = off
    n = len(buf)
    while e + 1 < n and (buf[e] or buf[e + 1]):
        e += 2
    return buf[off:e].decode('utf-16-le', 'surrogatepass'), e + 2


def wenc(s):
    return s.encode('utf-16-le', 'surrogatepass') + b'\0\0'


def toshi_hash(name):
    """TTRB::HashString (TTRB.cpp:330-342): hash = (TINT16)char + hash * 31, TCHAR is signed."""
    h = 0
    for ch in name.encode('latin-1'):
        if ch >= 0x80:
            ch -= 0x100
        h = (ch + h * 0x1f) & 0xFFFF
    return h - 0x10000 if h >= 0x8000 else h


def btec_decompress(buf, off):
    """Returns (data, bytes consumed)."""
    magic, ver, csize, size = struct.unpack_from('<4sIII', buf, off)
    if magic != b'BTEC':
        raise ValueError('BTEC magic expected')
    hdr = 16
    xor = None
    if ver == 0x00010003:
        xor = buf[off + 16]
        hdr = 20
    elif ver != 0x00010002:
        raise ValueError('BTEC version %x' % ver)
    p = off + hdr
    end = p + csize
    out = bytearray()
    while p < end:
        b = buf[p]
        p += 1
        n = b & 0x3F
        if b & 0x40:
            n = (n << 8) | buf[p]
            p += 1
        n += 1
        if b & 0x80:                       # raw bytes from the file
            out += buf[p:p + n]
            p += n
        else:                              # copy of the unpacked data, may overlap
            o = buf[p]
            p += 1
            if o & 0x80:
                o = ((o & 0x7F) << 8) | buf[p]
                p += 1
            o += 1
            s = len(out) - o
            if o >= n:
                out += out[s:s + n]
            else:
                for k in range(n):
                    out.append(out[s + k])
    if xor is not None:
        out = bytearray(x ^ xor for x in out)
    if len(out) != size:
        raise ValueError('BTEC size mismatch')
    return bytes(out), hdr + csize


class TRB:
    def __init__(self, data, name=''):
        self.name = name
        self.raw = data
        if data[0:4] != b'TSFL' or data[8:12] != b'TRBF':
            raise ValueError('%s: not a little endian TRB' % name)
        self.hunks = []          # (name, data offset, size, pad bytes)
        pos = 12
        end = 8 + u32(data, 4)
        while pos < end:
            hn = data[pos:pos + 4].decode('latin-1')
            sz = u32(data, pos + 4)
            self.hunks.append((hn, pos + 8, sz, data[pos + 8 + sz:pos + 8 + align(sz, 4)]))
            pos += 8 + align(sz, 4)
        H = {h[0]: h for h in self.hunks}
        o = H['HDRX'][1]
        self.version, n = struct.unpack_from('<II', data, o)
        self.secinfo = [struct.unpack_from('<hhIII', data, o + 8 + 16 * i) for i in range(n)]
        self.compressed = 'SECC' in H
        self.sections = []
        if self.compressed:
            p = H['SECC'][1]
            for si in self.secinfo:
                sec, used = btec_decompress(data, p)
                p += used
                self.sections.append(sec)
        else:
            p = H['SECT'][1]
            for si in self.secinfo:
                self.sections.append(data[p:p + si[2]])
                p += si[2]
        self.relocs = []
        if 'RELC' in H:
            o = H['RELC'][1]
            for i in range(u32(data, o)):
                self.relocs.append(struct.unpack_from('<hhI', data, o + 4 + 8 * i))
        self.rel = {(r[0], r[2]): r[1] for r in self.relocs}
        self.symbols = []
        if 'SYMB' in H:
            o, sz = H['SYMB'][1], H['SYMB'][2]
            n = u32(data, o)
            names = data[o + 4 + 12 * n:o + sz]
            for i in range(n):
                hdrx, noff, pad, h, doff = struct.unpack_from('<HHHhI', data, o + 4 + 12 * i)
                self.symbols.append(dict(sec=hdrx, name=names[noff:names.index(b'\0', noff)].decode('latin-1'),
                                         off=doff, hash=h, pad=pad, name_off=noff))
        self.symmap = {s['name']: s for s in self.symbols}

    def sym(self, name):
        s = self.symmap.get(name)
        return (s['sec'], s['off']) if s else None

    def ptr(self, sec, off):
        """Pointer field at (sec, off): (target section, offset) or None for a NULL pointer (TTRB.cpp:176-192)."""
        t = self.rel.get((sec, off))
        if t is None:
            return None
        return (t, u32(self.sections[sec], off))

    def memory_size(self):
        """Bytes T2Locale's bump allocator takes for this file: the HDRX hunk (TTRB.cpp:223), every section
        (TTRB.cpp:230) and the SYMB hunk (TTRB.cpp:133). TRBAlloc doesn't align (T2Locale.h:51-56)."""
        H = {h[0]: h for h in self.hunks}
        return H['HDRX'][2] + sum(si[2] for si in self.secinfo) + (H['SYMB'][2] if 'SYMB' in H else 0)


def build_trb(sections, relocs, symbols, version=0x00010001):
    """Writes a TRB like the retail files: hunks HDRX, SECT, RELC, SYMB, each padded with zeros to 4.
    sections: list of bytes, relocs: list of (pointer section, target section, offset),
    symbols: list of (section, name, offset)."""
    def hunk(name, payload):
        return name + struct.pack('<I', len(payload)) + payload + b'\0' * (align(len(payload), 4) - len(payload))

    hdrx = struct.pack('<II', version, len(sections)) + b''.join(struct.pack('<hhIII', 0, 0, len(s), 0, 0) for s in sections)
    sect = b''.join(sections)
    relc = struct.pack('<I', len(relocs)) + b''.join(struct.pack('<hhI', a, b, o) for a, b, o in relocs)
    names = b''
    ents = b''
    for sec, name, off in symbols:
        ents += struct.pack('<HHHhI', sec, len(names), 0, toshi_hash(name), off)
        names += name.encode('latin-1') + b'\0'
    symb = struct.pack('<I', len(symbols)) + ents + names
    body = b'TRBF' + hunk(b'HDRX', hdrx) + hunk(b'SECT', sect) + hunk(b'RELC', relc) + hunk(b'SYMB', symb)
    return b'TSFL' + struct.pack('<I', len(body)) + body


def parse_locale(trb):
    sec, off = trb.sym('LocaleStrings')                       # T2Locale.cpp:65
    buf = trb.sections[sec]
    n = i32(buf, off)                                         # T2Locale.h:25-29
    tsec, toff = trb.ptr(sec, off + 4)
    out = []
    for i in range(n):
        ssec, soff = trb.ptr(tsec, toff + 4 * i)
        out.append(wstr_at(trb.sections[ssec], soff)[0])
    return out


def build_locale(strings):
    """Layout of every retail locale file: strings packed from 0 in id order, zeros to 16, the pointer table,
    zeros to 16, LocaleStrings {n, table}, 0xE1A00000 words to 16."""
    sec = bytearray()
    offs = []
    for s in strings:
        offs.append(len(sec))
        sec += wenc(s)
    sec += b'\0' * (align(len(sec), 16) - len(sec))
    table = len(sec)
    for o in offs:
        sec += struct.pack('<I', o)
    sec += b'\0' * (align(len(sec), 16) - len(sec))
    ls = len(sec)
    sec += struct.pack('<iI', len(strings), table)
    while len(sec) % 16:
        sec += ARM_NOP
    relocs = [(0, 0, table + 4 * i) for i in range(len(strings))] + [(0, 0, ls + 4)]
    return build_trb([bytes(sec)], relocs, [(0, 'LocaleStrings', ls)])


def parse_subs(trb):
    """Returns (version, [(symbol, [(start ms, end ms, text), ...]), ...]) in symbol order."""
    vs = trb.sym('SubtitleVersion')                            # ASubtitleManager.cpp:180
    version = u32(trb.sections[vs[0]], vs[1]) if vs else None
    entries = []
    for s in trb.symbols:
        if s['name'] == 'SubtitleVersion':
            continue
        buf = trb.sections[s['sec']]
        n = i32(buf, s['off'])                                 # ASubtitleManager.h:18-29
        lines = []
        for j in range(n):
            o = s['off'] + 4 + 12 * j
            t = trb.ptr(s['sec'], o + 8)
            text = wstr_at(trb.sections[t[0]], t[1])[0] if t else None
            lines.append((i32(buf, o), i32(buf, o + 4), text))
        entries.append((s['name'], lines))
    return version, entries


def build_subs(version, entries):
    """Layout of every retail subtitle file: section 0 = SubtitleVersion (int + three 0xE1A00000 words),
    section 1 = every Data struct in symbol order, then every line text in line order, zeros to 4."""
    sec0 = struct.pack('<I', version) + ARM_NOP * 3
    sec1 = bytearray()
    syms = [(0, 'SubtitleVersion', 0)]
    fixups = []
    for name, lines in entries:
        syms.append((1, name, len(sec1)))
        sec1 += struct.pack('<i', len(lines))
        for start, end, text in lines:
            fixups.append((len(sec1) + 8, text))
            sec1 += struct.pack('<iiI', start, end, 0)
    relocs = []
    for o, text in fixups:
        struct.pack_into('<I', sec1, o, len(sec1))
        sec1 += wenc(text)
        relocs.append((1, 1, o))
    sec1 += b'\0' * (align(len(sec1), 4) - len(sec1))
    return build_trb([sec0, bytes(sec1)], relocs, syms)
