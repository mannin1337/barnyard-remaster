# -*- coding: utf-8 -*-
"""Barnyard (2006) / OpenBarnyard language tools.

  python locale_tool.py --data <original-Data> --work <work-dir> export
  python locale_tool.py --data <original-Data> import --lang hun --in hu_strings.json hu_remaster.json --out <output-dir>
  python locale_tool.py --data <original-Data> --work <work-dir> roundtrip

Engine formats
  * TRB container  - Toshi/Source/File/TTSF.cpp, TTRB.cpp (TSFL/TRBF hunks: HDRX, SECT|SECC, RELC, SYMB)
  * locale table   - Toshi/Source/T2Locale/T2Locale.{h,cpp}, symbol "LocaleStrings" {int n; wchar_t** strings}
  * subtitles      - OpenBarnyard/Source/GUI/ASubtitleManager.{h,cpp}, a symbol per subtitle {int n; {int start, end; wchar_t* text}[n]}
  * text codes     - OpenBarnyard/Source/Locale/ALocale.cpp, Units/ASpeechActivity.cpp
  * BTEC (SECC)    - Toshi/Source/File/TCompress_Decompress.cpp
  * PBProperties   - Toshi/Source/Plugins/PPropertyParser/PBProperties.h
  * fonts          - Toshi/Source/GUI/T2GUIFont.h (T2GUIFontDef), Data/GUI/fonts.trb

Standard library only. Python 3.14 ("py"), set PYTHONIOENCODING=utf-8.
"""
import argparse
import collections
import json
import math
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEF_DATA = os.path.join(ROOT, 'Game', 'Data')
DEF_SRC = ROOT
DEF_WORK = HERE

LOCALE_BUFFER = 307200        # ALocaleManager.cpp:14, T2Locale bump allocator without an overflow check (T2Locale.h:51-56)
SPEECH_TEXT_MAX = 255         # ASpeechActivity.h:46 wszText[256], filled without a limit (ASpeechActivity.cpp:656-771)
TAG_ALLOWANCE = 20            # assumed length of a button name / character name that replaces a tag
ARM_NOP = b'\x00\x00\xa0\xe1'  # word 0xE1A00000 that the 2006 Toshi tools put at the end of some sections

HU_LETTERS = 'áéíóöőúüűÁÉÍÓÖŐÚÜŰ'   # the 18 Hungarian accented letters
SUB_LIBS = ('lib_misc', 'lib_sim', 'lib_sseqs')
# punctuation the fonts can't draw -> what they can (used by import unless --no-normalize)
PUNCT_MAP = {'\u201e': '"', '\u201d': '"', '\u201c': '"', '\u00bb': '"', '\u00ab': '"', '\u201a': "'", '\u2018': "'",
             '\u2019': "'", '\u2013': '-', '\u2014': '-', '\u2026': '...', '\u2212': '-', '\u2009': ' ', '\u202f': '\u00a0'}
# fallback if the fonts don't get the double acute letters (import --fallback-chars)
FALLBACK_MAP = {'ő': 'õ', 'Ő': 'Õ', 'ű': 'û', 'Ű': 'Û'}


# --------------------------------------------------------------------------------------------
# binary helpers
# --------------------------------------------------------------------------------------------
def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def i32(b, o):
    return struct.unpack_from('<i', b, o)[0]


def align(n, a):
    return (n + a - 1) // a * a


def read_file(path):
    with open(path, 'rb') as f:
        return f.read()


def write_file(path, data):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def write_text(path, text):
    write_file(path, text.encode('utf-8'))


def wstr_at(buf, off):
    """NUL terminated UTF-16LE string (lone surrogates survive the round trip)."""
    e = off
    n = len(buf)
    while e + 1 < n and (buf[e] or buf[e + 1]):
        e += 2
    return buf[off:e].decode('utf-16-le', 'surrogatepass'), e + 2


def cstr_at(buf, off):
    e = buf.index(b'\0', off)
    return buf[off:e].decode('latin-1')


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


# --------------------------------------------------------------------------------------------
# BTEC (SECC hunks) - TCompress_Decompress.cpp:89-240
# --------------------------------------------------------------------------------------------
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


# --------------------------------------------------------------------------------------------
# TRB container - TTSF.cpp (hunks), TTRB.cpp:75-250 (ProcessForm)
# --------------------------------------------------------------------------------------------
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


def engine_load(data, symbol, base=0x01000000):
    """Loads a TRB the way TTRB::ProcessForm does (TTRB.cpp:75-250: hunk names compared as numbers against "HEAD",
    leftSize bookkeeping, RELC adds the section addresses), with T2Locale's unaligned bump allocator
    (T2Locale.h:51-56), and looks the symbol up like GetSymbolIndex (hash, then name; TTRB.cpp:267-288).
    Returns (memory taken, {address: bytes of the section at that address}, symbol address)."""
    HEAD = struct.unpack('<I', b'HEAD')[0]
    if data[0:4] != b'TSFL' or data[8:12] != b'TRBF':
        raise ValueError('not TSFL/TRBF')
    left = u32(data, 4) - 4                                   # ProcessForm: m_CurrentHunk.Size - 4
    pos = 12
    mem = 0
    secs = []                                                 # [address, size, bytearray]
    symb = None
    while left >= 1:
        name = data[pos:pos + 4]
        size = u32(data, pos + 4)
        body = data[pos + 8:pos + 8 + size]
        left -= align(size, 4) + 8
        nv = struct.unpack('<I', name)[0]
        if nv > HEAD:
            if name == b'HDRX':
                mem += size                                   # AllocType_Header with the hunk size
                n = u32(body, 4)
                for i in range(n):
                    ssize = u32(body, 8 + 16 * i + 4)
                    secs.append([base + mem, ssize, None])
                    mem += ssize                              # AllocType_Data
            elif name == b'SECT':
                p = 0
                for s in secs:
                    s[2] = bytearray(body[p:p + s[1]])
                    p += s[1]
        elif name == b'SYMB':
            mem += size
            symb = body
        elif name == b'RELC':
            for i in range(u32(body, 0)):
                h1, h2, off = struct.unpack_from('<hhI', body, 4 + 8 * i)
                v = u32(secs[h1][2], off) + secs[h2][0]
                struct.pack_into('<I', secs[h1][2], off, v)
        pos += 8 + align(size, 4)
    want = toshi_hash(symbol)
    n = u32(symb, 0)
    for i in range(n):
        hdrx, noff, pad, h, doff = struct.unpack_from('<HHHhI', symb, 4 + 12 * i)
        nstart = 4 + 12 * n + noff                             # GetSymbolName: m_SYMB + GetSymbolTableSize(n) + NameOffset
        if h == want and symb[nstart:symb.index(b'\0', nstart)] == symbol.encode('latin-1'):
            return mem, {s[0]: s[2] for s in secs}, secs[hdrx][0] + doff
    return mem, {s[0]: s[2] for s in secs}, None


def engine_read(mem_map, addr, fmt):
    for a, b in mem_map.items():
        if a <= addr < a + len(b):
            return struct.unpack_from(fmt, b, addr - a)
    raise ValueError('address %x outside the loaded sections' % addr)


def engine_wstr(mem_map, addr):
    out = []
    while True:
        c = engine_read(mem_map, addr, '<H')[0]
        if not c:
            return bytes(struct.pack('<%dH' % len(out), *out)).decode('utf-16-le', 'surrogatepass')
        out.append(c)
        addr += 2


def engine_locale(data):
    """T2Locale::SetLanguage + GetString (T2Locale.cpp:59-87) on the simulated load: (memory, strings)."""
    mem, mm, addr = engine_load(data, 'LocaleStrings')
    n, table = engine_read(mm, addr, '<iI')
    return mem, [engine_wstr(mm, engine_read(mm, table + 4 * i, '<I')[0]) for i in range(n)]


def engine_subtitle(data, name):
    """ASubtitleManager::PlaySubtitle (ASubtitleManager.cpp:197-219) on the simulated load: [(start, end, text)]."""
    mem, mm, addr = engine_load(data, name)
    if addr is None:
        return None
    n = engine_read(mm, addr, '<i')[0]
    out = []
    for j in range(n):
        st, en, p = engine_read(mm, addr + 4 + 12 * j, '<iiI')
        out.append((st, en, engine_wstr(mm, p)))
    return out


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


# --------------------------------------------------------------------------------------------
# locale table (Data/Locale/<lang>.trb)
# --------------------------------------------------------------------------------------------
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


# --------------------------------------------------------------------------------------------
# subtitles (Data/Subs/<LANG>/<lib>.trb)
# --------------------------------------------------------------------------------------------
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


# --------------------------------------------------------------------------------------------
# fonts (Data/GUI/fonts.trb, a symbol per font) - T2GUIFont.h:9-36, T2GUIFontManager.cpp:183-211
# --------------------------------------------------------------------------------------------
def parse_fonts(trb):
    fonts = {}
    for s in trb.symbols:
        b = trb.sections[s['sec']]
        o = s['off']
        nmat = b[o + 11]
        mx, mn = struct.unpack_from('<HH', b, o + 12)
        mp = trb.ptr(s['sec'], o + 20)
        chars = set()
        if mp:
            for i, v in enumerate(struct.unpack_from('<%di' % (mx - mn + 1), trb.sections[mp[0]], mp[1])):
                if v >= 0:
                    chars.add(chr(mn + i))
        fonts[s['name']] = dict(chars=chars, min=mn, max=mx, materials=nmat)
    return fonts


# --------------------------------------------------------------------------------------------
# PBProperties walker (PBProperties.h:17-50, 769-1311; 32 bit layout)
# --------------------------------------------------------------------------------------------
PB_TYPES = ('Int', 'UInt32', 'Float', 'Bool', 'Properties', 'PropertyName', 'Array', 'String', 'LocaleString')


def pb_walk(trb, sec, off, path, out, depth=0):
    """Appends (path, type, value) for every leaf of the PBProperties at (sec, off).
    A nested block gets '#<index in its parent>' ("Main/activityset#0/speech#12/message")."""
    if depth > 64:
        return
    buf = trb.sections[sec]
    count = u32(buf, off + 8)
    arr = trb.ptr(sec, off + 4)
    if not arr or count > 100000:
        return
    asec, aoff = arr
    for i in range(count):
        np_ = trb.ptr(asec, aoff + 8 * i)
        vp = trb.ptr(asec, aoff + 8 * i + 4)
        name = '?'
        if np_:
            sp = trb.ptr(np_[0], np_[1])
            if sp:
                name = cstr_at(trb.sections[sp[0]], sp[1])
        if vp:
            pb_value(trb, vp[0], vp[1], path + '/' + name, out, depth, i)


def pb_value(trb, sec, off, path, out, depth, index=None):
    buf = trb.sections[sec]
    t = u32(buf, off)
    tn = PB_TYPES[t] if t < len(PB_TYPES) else 'T%d' % t
    if tn == 'Properties':
        p = trb.ptr(sec, off + 4)
        if p:
            pb_walk(trb, p[0], p[1], path if index is None else '%s#%d' % (path, index), out, depth + 1)
    elif tn == 'Array':
        p = trb.ptr(sec, off + 4)
        if p:
            n = u32(trb.sections[p[0]], p[1] + 4)
            vals = trb.ptr(p[0], p[1])
            if vals and n < 100000:
                for k in range(n):
                    pb_value(trb, vals[0], vals[1] + 8 * k, '%s[%d]' % (path, k), out, depth + 1)
    elif tn == 'String':
        p = trb.ptr(sec, off + 4)
        out.append((path, tn, cstr_at(trb.sections[p[0]], p[1]) if p else None))
    elif tn == 'PropertyName':
        p = trb.ptr(sec, off + 4)
        v = None
        if p:
            q = trb.ptr(p[0], p[1])
            v = cstr_at(trb.sections[q[0]], q[1]) if q else None
        out.append((path, tn, v))
    elif tn == 'Float':
        out.append((path, tn, struct.unpack_from('<f', buf, off + 4)[0]))
    elif tn == 'Int':
        out.append((path, tn, i32(buf, off + 4)))
    else:
        out.append((path, tn, u32(buf, off + 4)))


def pb_leaves(trb):
    out = []
    for s in trb.symbols:
        if s['name'] == 'Main' or s['name'].endswith('_Main'):
            try:
                pb_walk(trb, s['sec'], s['off'], s['name'], out)
            except (IndexError, struct.error, ValueError):
                pass
    return out


# --------------------------------------------------------------------------------------------
# text codes
# --------------------------------------------------------------------------------------------
CODE_RE = re.compile(
    r'%c_[A-Za-z0-9_]+\[[A-Za-z0-9_]*\]'                                  # input tag, ALocale.cpp:292-381
    r'|%\{[^}]*\}'                                                        # %{N:spec}, ALocale.cpp:184-230
    r'|%(?:player|[aps])[^A-Za-z0-9\s%{](?:name|species|nbpc|rank)'       # speech tag, ASpeechActivity.cpp:671-768
    r'|%%'                                                                # literal percent, ALocale.cpp:239-244
    r'|%[1-9]'                                                            # argument N, ALocale.cpp:232-238
    r'|%[-+#0]*\d*(?:\.\d+)?(?:hh|h|ll|l|I64|I32|w)?[diouxXeEfgGcsSp]'   # printf (String16Format callers)
    r'|%'                                                                 # lone percent, printed as is
    r'|\r\n|\n|\r|\t')
PRINTF_RE = re.compile(r'%[-+#0]*\d*(?:\.\d+)?(?:hh|h|ll|l|I64|I32|w)?[diouxXeEfgGcsSp]$')
BREAK_RE = re.compile(r'\r\n|\n|\r|\t')
SPEECH_TAG_RE = re.compile(r'%(?:player|[aps])[^A-Za-z0-9\s%{](?:name|species|nbpc|rank)$')


def codes_of(text, plain=False):
    """Codes of a text in order. plain: the text is drawn as it is (subtitles: AGUI2TextBox::SetText,
    AGUI2TextBox.cpp:42-46, no tag processing), only line breaks count."""
    return (BREAK_RE if plain else CODE_RE).findall(text or '')


def code_kind(c):
    if c.startswith('%c_'):
        return 'input'
    if c.startswith('%{'):
        return 'arg'
    if SPEECH_TAG_RE.match(c):
        return 'speech'
    if c == '%%' or c == '%':
        return 'percent'
    if re.match(r'%[1-9]$', c):
        return 'arg'
    if PRINTF_RE.match(c):
        return 'printf'
    return 'break'


def split_codes(text):
    """[(is_code, piece), ...]"""
    out = []
    pos = 0
    for m in CODE_RE.finditer(text):
        if m.start() > pos:
            out.append((False, text[pos:m.start()]))
        out.append((True, m.group()))
        pos = m.end()
    if pos < len(text):
        out.append((False, text[pos:]))
    return out


def expanded_len(text):
    """Worst case length once the engine replaced the tags (button names, character names, numbers)."""
    n = 0
    for is_code, piece in split_codes(text):
        if not is_code:
            n += len(piece)
        else:
            k = code_kind(piece)
            n += TAG_ALLOWANCE if k in ('input', 'speech') else (10 if k in ('arg', 'printf') else 1)
    return n


# --------------------------------------------------------------------------------------------
# engine source scan: which locale ids the C++ code uses, and the buffers they go to
# --------------------------------------------------------------------------------------------
FUNC_RE = re.compile(r'^(?:[A-Za-z_][\w:<>,\*&\s]*?\s+[\*&]?)?((?:[A-Za-z_]\w*::)+~?[A-Za-z_]\w*)\s*\(')
NUM_RE = r'(?:0x[0-9A-Fa-f]+|\d+)'
AREA_NAMES = {'GameInterface': 'menu/screen', 'HUD': 'HUD', 'GUI': 'GUI', 'MiniGame': 'minigame', 'Units': 'game world',
              'Quests': 'quests', 'Player': 'player', 'Helpers': 'game', 'Locale': 'formatting', 'Cameras': 'camera',
              'FX': 'effects', 'Tasks': 'game', 'Assets': 'assets', 'Sound': 'sound'}


def _num(s):
    return int(s, 16) if s.lower().startswith('0x') else int(s)


def scan_source(src_dir):
    """Returns (refs, limits): refs[id] = [(rel path, line, function, how)], limits[id] = [(max chars, kind, where)]."""
    base = os.path.join(src_dir, 'OpenBarnyard', 'Source')
    files = {}
    for root, dirs, fs in os.walk(base):
        for f in fs:
            if f.endswith(('.cpp', '.h')):
                p = os.path.join(root, f)
                with open(p, 'r', encoding='utf-8', errors='replace') as fh:
                    files[os.path.relpath(p, base).replace('\\', '/')] = fh.read()
    consts = collections.defaultdict(dict)      # name -> {file: value}  (constexpr / const / enum / #define only)
    arrays = collections.defaultdict(dict)      # name -> {file: size expr}
    inits = collections.defaultdict(dict)       # name -> {file: initializer text}
    for rel, text in files.items():
        for m in re.finditer(r'\b(?:constexpr|const)\b[^;=()]*?\b([A-Za-z_]\w*)\s*=\s*(-?' + NUM_RE + r')\s*[;,]', text):
            consts[m.group(1)][rel] = _num(m.group(2).lstrip('-')) * (-1 if m.group(2).startswith('-') else 1)
        for m in re.finditer(r'^\s*([A-Z][A-Z0-9_]+)\s*=\s*(' + NUM_RE + r')\s*,', text, re.M):
            consts[m.group(1)].setdefault(rel, _num(m.group(2)))
        for m in re.finditer(r'^\s*#\s*define\s+([A-Za-z_]\w*)\s+(' + NUM_RE + r')\s*$', text, re.M):
            consts[m.group(1)].setdefault(rel, _num(m.group(2)))
        for m in re.finditer(r'\bTWCHAR\s+\*?\s*([A-Za-z_]\w*)\s*\[\s*([^\]]+?)\s*\]', text):
            arrays[m.group(1)].setdefault(rel, m.group(2))
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S):
            inits[m.group(1)][rel] = re.sub(r'//[^\n]*', '', m.group(2))

    def lookup(table, name, rel):
        d = table.get(name)
        if not d:
            return None
        if rel in d:
            return d[rel]
        h = rel.rsplit('.', 1)[0] + '.h'
        if h in d:
            return d[h]
        vals = set(map(str, d.values()))
        if len(vals) == 1:
            return next(iter(d.values()))
        return None

    def ids_of(expr, rel):
        expr = expr.strip()
        m = re.search(r'GetString\(\s*(.*)\)\s*$', expr)
        if m:
            return ids_of(m.group(1), rel)
        if re.fullmatch(NUM_RE, expr):
            return [_num(expr)]
        if re.fullmatch(r'[A-Za-z_]\w*', expr):
            v = lookup(consts, expr, rel)
            return [v] if isinstance(v, int) else []
        m = re.fullmatch(r'([A-Za-z_]\w*)\s*\[.*\](\s*\.\s*\w+)?', expr)
        if m:
            init = lookup(inits, m.group(1), rel)
            if init is None:
                return []
            if m.group(2):                           # array of {screen name id, text id} pairs only
                groups = [re.findall(r'-?' + NUM_RE, g) for g in re.findall(r'\{([^{}]*)\}', init)]
                if not groups or any(len(g) != 2 for g in groups):
                    return []
                vals = [g[1] for g in groups]
            else:
                vals = re.findall(r'-?' + NUM_RE + r'|[A-Za-z_]\w*', init)
            out = []
            for v in vals:
                if v.startswith('-'):
                    continue
                if re.fullmatch(NUM_RE, v):
                    out.append(_num(v))
                else:
                    c = lookup(consts, v, rel)
                    if isinstance(c, int):
                        out.append(c)
            return out
        return []

    def size_of(expr, rel):
        expr = expr.strip()

        def arr(m):
            v = lookup(arrays, m.group(1), rel)
            return '(%s)' % v if v is not None else 'X'
        e = re.sub(r'TARRAYSIZE\(\s*([A-Za-z_]\w*)\s*\)', arr, expr)
        for _ in range(3):
            e = re.sub(r'\b([A-Za-z_]\w*)\b', lambda m: str(lookup(consts, m.group(1), rel)) if isinstance(lookup(consts, m.group(1), rel), int) else m.group(1), e)
        if re.fullmatch(r'[\d\s\+\-\*\(\)x]+', e) and 'X' not in e:
            try:
                return int(eval(e, {'__builtins__': {}}))
            except Exception:
                return None
        return None

    refs = collections.defaultdict(list)
    limits = collections.defaultdict(list)
    for rel, text in sorted(files.items()):
        lines = text.split('\n')
        func = ''
        for ln, line in enumerate(lines, 1):
            fm = FUNC_RE.match(line)
            if fm and not line.rstrip().endswith(';'):
                func = fm.group(1)
            code = line.split('//', 1)[0]
            where = (rel, ln, func)
            # ALocale::FormatTags / FormatInputTags( buffer, size, id or string, ... )
            for m in re.finditer(r'ALocale::Format(Input)?Tags\(\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*((?:[^(),]|\((?:[^()]|\([^()]*\))*\))+)', code):
                ids = ids_of(m.group(4), rel)
                size = size_of(m.group(3), rel)
                for i in ids:
                    refs[i].append(where + ('Format%sTags' % (m.group(1) or ''),))
                    if size:
                        limits[i].append((size - 1, 'truncated', '%s:%d' % (rel, ln)))
            # g_pLocaleManager->GetString( id ) / ALocaleManager::Interface()->GetString( id )
            for m in re.finditer(r'(?:LocaleManager|Interface\(\)|T2Locale::GetSingleton\(\))\s*->\s*GetString\(\s*((?:[^()]|\((?:[^()]|\([^()]*\))*\))+?)\s*\)', code):
                for i in ids_of(m.group(1), rel):
                    if not any(r[:2] == where[:2] for r in refs[i]):
                        refs[i].append(where + ('GetString',))
                # unbounded copy of a locale string into a fixed buffer
                cm = re.search(r'String16Copy\(\s*([A-Za-z_][\w>.\-]*)\s*,', code)
                if cm:
                    size = lookup(arrays, cm.group(1).split('->')[-1].split('.')[-1], rel)
                    size = size_of(str(size), rel) if size is not None else None
                    if size:
                        for i in ids_of(m.group(1), rel):
                            limits[i].append((size - 1, 'overflow', '%s:%d' % (rel, ln)))
            # a named locale constant used anywhere (assigned to a variable first, passed on...)
            for m in re.finditer(r'\b(LOCALE_[A-Z0-9_]+)\b(?!\s*=)', code):
                v = lookup(consts, m.group(1), rel)
                if isinstance(v, int) and v >= 0 and not any(r[:2] == where[:2] for r in refs[v]):
                    refs[v].append(where + ('constant ' + m.group(1),))
            # comments naming a locale id: "locale 549", "locale 0x202"
            for m in re.finditer(r'//.*?\blocale (?:id )?(' + NUM_RE + r')\b', line, re.I):
                i = _num(m.group(1))
                if not any(r[:2] == where[:2] for r in refs[i]):
                    refs[i].append(where + ('comment',))
    return refs, limits


# --------------------------------------------------------------------------------------------
# game data scan: speech, quests, unit names, credits, recipes
# --------------------------------------------------------------------------------------------
def _load_leaves(path):
    try:
        return pb_leaves(TRB(read_file(path), path))
    except (OSError, ValueError, KeyError):
        return []


def scan_data(data_dir, eng):
    """Returns ctx[id] = [context strings], speech = set of ids used by speech activities."""
    facts = collections.defaultdict(lambda: collections.defaultdict(list))     # id -> kind -> [details]
    speech_ids = set()
    J = os.path.join

    # unit names: Units/AllUnitTypes.trz, Main/<type>#k/<component>#j/ScreenName (AUnitType.cpp:50-51)
    unit_names = {'player': 'the player (the cow the player made)'}
    for p, tn, v in _load_leaves(J(data_dir, 'Units', 'AllUnitTypes.trz')):
        if tn == 'LocaleString' and p.endswith('/ScreenName'):
            parts = p.split('/')
            utype = parts[1].split('#')[0]
            comp = parts[2].split('#')[0] if len(parts) > 3 else ''
            if comp == 'Unit':
                facts[v]['unit'].append(utype)
                if 0 <= v < len(eng):
                    unit_names.setdefault(utype.lower(), eng[v])
            else:
                facts[v]['item'].append('%s (%s)' % (utype, comp))

    def who(name):
        n = (name or '').lower()
        return unit_names.get(n, name)

    # quests: Quests/editor_quests__1.trb (AQuest.cpp:83, 93; AQuest.h:69, 79)
    quests = collections.defaultdict(dict)
    for p, tn, v in _load_leaves(J(data_dir, 'Quests', 'editor_quests__1.trb')):
        m = re.match(r'Main/quest#(\d+)/(\w+)$', p)
        if m:
            quests[int(m.group(1))][m.group(2)] = v
    for q in quests.values():
        d = '"%s" (from %s)' % (q.get('name'), who(q.get('quest_giver')))
        if isinstance(q.get('title'), int):
            facts[q['title']]['qtitle'].append(d)
        if isinstance(q.get('xsummary'), int):
            facts[q['xsummary']]['qsum'].append(d)

    # interactions + activity sets: Interactions/AllInteractions.trz, ActivitySets1.trz
    # (AInteractionManager.cpp:806-868, AInteraction.cpp:627-650, ASpeechActivity.cpp:140-143)
    reqs = collections.defaultdict(dict)
    inter = collections.defaultdict(lambda: dict(reqs=[], acts=[], fields={}))
    for p, tn, v in _load_leaves(J(data_dir, 'Interactions', 'AllInteractions.trz')):
        m = re.match(r'Main/requirements#(\d+)/(\w+)#(\d+)/(\w+)(?:\[\d+\])?$', p)
        if m:
            r = reqs[(int(m.group(1)), int(m.group(3)))]
            r['kind'] = m.group(2)
            if m.group(4) == 'requirements':
                r.setdefault('children', []).append(v)
            else:
                r[m.group(4)] = v
            continue
        m = re.match(r'Main/interaction#(\d+)/(.*)$', p)
        if m:
            it = inter[int(m.group(1))]
            key = m.group(2)
            if key.startswith('requirements['):
                it['reqs'].append(v)
            elif '/_elem[' in key:
                it['acts'].append(v)
            elif tn == 'LocaleString' and key == 'hudtext':
                it['fields']['hudtext'] = v
            else:
                it['fields'][key.split('[')[0]] = v
    byname = {}
    for r in reqs.values():
        if r.get('name'):
            byname[r['name']] = r

    def roles(name, seen=None):
        seen = seen or set()
        if name in seen:
            return set()
        seen.add(name)
        r = byname.get(name)
        out = set()
        if r:
            if r.get('kind') in ('unittype', 'unitname') and not r.get('invert') and r.get('category') and r.get('match'):
                out.add((r['category'], r['match']))
            for c in r.get('children', []):
                out |= roles(c, seen)
        m = re.match(r'(Primary|Secondary) (?:is|=) ([A-Za-z_\' ]+)$', name or '')
        if m and not out:
            out.add((m.group(1).lower(), m.group(2).strip()))
        return out

    act_roles = collections.defaultdict(set)
    act_next = {}
    for it in inter.values():
        rs = set()
        for rn in it['reqs']:
            rs |= roles(rn)
        for a in it['acts']:
            act_roles[a] |= rs
        if 'hudtext' in it['fields']:                  # AInteraction.h:245
            facts[it['fields']['hudtext']]['hud'].append(it['fields'].get('trigger') or '?')
        for k, a in enumerate(it['acts']):
            act_next.setdefault(a, it['acts'][k - 1] if k else None)
    acts = collections.defaultdict(dict)
    for p, tn, v in _load_leaves(J(data_dir, 'Interactions', 'ActivitySets1.trz')):
        m = re.match(r'Main/activityset#\d+/(\w+)#(\d+)/(\w+)$', p)
        if m:
            a = acts[int(m.group(2))]
            a['kind'] = m.group(1)
            a[m.group(3)] = v
    for idx in sorted(acts):
        a = acts[idx]
        if a.get('kind') != 'speech' or not isinstance(a.get('message'), int):
            continue
        mid = a['message']
        speech_ids.add(mid)
        cat = a.get('category', 'primary')
        rs = act_roles.get(idx, set())
        spk = sorted({who(u) for c, u in rs if c == cat})
        lst = sorted({who(u) for c, u in rs if c != cat})
        facts[mid]['speech'].append('%s%s' % (' or '.join(spk) if spk else '(the %s unit)' % cat, ' to ' + ' or '.join(lst) if lst else ''))
        prv = act_next.get(idx)
        if prv is not None and acts.get(prv, {}).get('kind') == 'speech' and isinstance(acts[prv].get('message'), int):
            facts[mid]['prev'].append('locale:%04d' % acts[prv]['message'])

    # credits: credits.trb, Title / Name Int values are locale ids (ACreditsState.cpp:280-292)
    for p, tn, v in _load_leaves(J(data_dir, 'credits.trb')):
        if tn == 'Int' and p in ('Main/Title', 'Main/Name'):
            facts[v]['credits'].append('section title' if p.endswith('Title') else 'line')

    # cooking recipes: MiniGameData/CookingGame/cooking.trb AllRecipes (ACookingRecipe.h:92-101)
    try:
        t = TRB(read_file(J(data_dir, 'MiniGameData', 'CookingGame', 'cooking.trb')))
        sec, off = t.sym('AllRecipes')
        n = i32(t.sections[t.sym('NumberOfRecipes')[0]], t.sym('NumberOfRecipes')[1])
        for k in range(n):
            o = off + 0x24 * k
            p = t.ptr(sec, o + 4)
            facts[i32(t.sections[sec], o)]['recipe'].append('%s, price %d' % (cstr_at(t.sections[p[0]], p[1]) if p else '?', i32(t.sections[sec], o + 8)))
    except (OSError, ValueError, KeyError, TypeError):
        pass

    def lst(xs, n=3):
        u = list(dict.fromkeys(xs))
        return ', '.join(u[:n]) + (' +%d more' % (len(u) - n) if len(u) > n else '')
    ctx = collections.defaultdict(list)
    for i, f in facts.items():
        c = ctx[i]
        if f['unit']:
            c.append('name of a character/thing (unit type %s): shown in prompts, in speech bubbles in upper case' % lst(f['unit']))
        if f['item']:
            c.append('name of %s' % lst(f['item']))
        if f['recipe']:
            c.append('cooking minigame: name of a dish/drink (%s)' % lst(f['recipe']))
        if f['qtitle']:
            c.append('quest offer message (the quest title, sent by the quest giver) of quest %s' % lst(f['qtitle']))
        if f['qsum']:
            c.append('quest log summary of quest %s' % lst(f['qsum']))
        if f['hud']:
            c.append('HUD prompt at an interaction spot (trigger %s)' % lst(f['hud']))
        if f['speech']:
            c.append('dialogue line (speech bubble, voiced in English): %s' % lst(f['speech']) + ('; previous line %s' % lst(f['prev'], 2) if f['prev'] else ''))
        if f['credits']:
            c.append('credits: %s' % lst(f['credits']))
    return ctx, speech_ids


# --------------------------------------------------------------------------------------------
# hardcoded English of the C++ code (not in the locale table)
# --------------------------------------------------------------------------------------------
CODE_STRINGS = [
    # (key, file under OpenBarnyard/Source, English, note)
    ('mainmenu.coop', 'GameInterface/AFrontEndMainMenuState2.cpp', 'Co-op', 'main menu button (mod)'),
    ('coop.title', 'GameInterface/AFrontEndCoopState.cpp', 'Co-op', 'title of the co-op menu (mod)'),
    ('coop.controller', 'GameInterface/AFrontEndCoopState.cpp', 'Player 2: controller %d', 'co-op menu, %d = controller number (mod)'),
    ('coop.nocontroller', 'GameInterface/AFrontEndCoopState.cpp', 'Player 2: no controller found. Connect one and restart the game.', 'co-op menu (mod)'),
    ('coop.host', 'GameInterface/AFrontEndCoopState.cpp', 'Host LAN Game', 'co-op menu button (mod)'),
    ('coop.join', 'GameInterface/AFrontEndCoopState.cpp', 'Join LAN Game', 'co-op menu button (mod)'),
    ('coop.samescreen', 'GameInterface/AFrontEndCoopState.cpp', 'Same Screen', 'co-op menu button (mod)'),
    ('coop.longeststatus', 'GameInterface/AFrontEndCoopState.cpp', 'Hosting on 255.255.255.255. Waiting for player 2...',
     'never shown: the code measures the status line with it, use the longest translated status text (mod)'),
    ('coopnet.hosting_connected', 'Player/ACoopNet.cpp', 'Hosting. Player 2 is connected (%s).', 'LAN status line, %s = IP address; 8 bit text in the code (mod)'),
    ('coopnet.hosting_on', 'Player/ACoopNet.cpp', 'Hosting on %s. Waiting for player 2...', 'LAN status line, %s = IP address; 8 bit text in the code (mod)'),
    ('coopnet.hosting', 'Player/ACoopNet.cpp', 'Hosting. Waiting for player 2...', 'LAN status line; 8 bit text in the code (mod)'),
    ('coopnet.connected', 'Player/ACoopNet.cpp', 'Connected to the host at %s.', 'LAN status line, %s = IP address; 8 bit text in the code (mod)'),
    ('coopnet.looking', 'Player/ACoopNet.cpp', 'Looking for a host on the LAN...', 'LAN status line; 8 bit text in the code (mod)'),
    ('options.hd_on', 'GameInterface/AOptionsState.cpp', 'HD Textures: On', 'options menu button (mod)'),
    ('options.hd_off', 'GameInterface/AOptionsState.cpp', 'HD Textures: Off', 'options menu button (mod)'),
    ('options.hd_on_restart', 'GameInterface/AOptionsState.cpp', 'HD Textures: On (restart)', 'options menu button (mod)'),
    ('options.hd_off_restart', 'GameInterface/AOptionsState.cpp', 'HD Textures: Off (restart)', 'options menu button (mod)'),
    ('camera.button', 'GameInterface/ACameraOptionsState.cpp', 'Camera', 'options menu button (community patch)'),
    ('camera.speed', 'GameInterface/ACameraOptionsState.cpp', 'Speed', 'camera options (community patch)'),
    ('camera.wiistyle', 'GameInterface/ACameraOptionsState.cpp', 'Wii Style', 'camera options (community patch)'),
    ('camera.followjumps', 'GameInterface/ACameraOptionsState.cpp', 'Follow Jumps', 'camera options (community patch)'),
    ('difficulty.easy', 'GameInterface/ADifficultyOptionState.cpp', 'Easy', 'difficulty option (community patch)'),
    ('difficulty.normal', 'GameInterface/ADifficultyOptionState.cpp', 'Normal', 'difficulty option (community patch)'),
    ('difficulty.title', 'GameInterface/ADifficultyOptionState.cpp', 'Difficulty', 'difficulty option title (community patch)'),
    ('antics.nocontrollers', 'GameInterface/AFrontEndPlayersState.cpp', 'Insufficient controllers detected for Barnyard Antics!', 'Barnyard Antics party menu'),
    ('language.name', 'GameInterface/AFrontEndLanguageState.cpp', 'English (UK)',
     'language menu: the name of THIS language written in this language (for Hungarian: Magyar)'),
    ('language.title', 'GameInterface/AFrontEndLanguageState.cpp', 'Language', 'language menu title, shown in the focused language'),
    ('language.ok', 'GameInterface/AFrontEndLanguageState.cpp', '%c_DialogInputMap[OK] Confirm', 'language menu prompt'),
    ('language.cancel', 'GameInterface/AFrontEndLanguageState.cpp', '%c_DialogInputMap[Cancel] Cancel', 'language menu prompt'),
]


MOD_MAX_LEN = {'coopnet.hosting_connected': 159, 'coopnet.hosting_on': 159, 'coopnet.hosting': 159,
               'coopnet.connected': 159, 'coopnet.looking': 159}       # ACoopNet.cpp:854 TCHAR szText[ 160 ]


def scan_code_strings(src_dir):
    base = os.path.join(src_dir, 'OpenBarnyard', 'Source')
    out = []
    cache = {}
    for key, rel, en, note in CODE_STRINGS:
        p = os.path.join(base, rel)
        if p not in cache:
            try:
                with open(p, 'r', encoding='utf-8', errors='replace') as fh:
                    cache[p] = fh.read().split('\n')
            except OSError:
                cache[p] = []
        line = next((i for i, l in enumerate(cache[p], 1) if ('"%s"' % en) in l), None)
        out.append(dict(key=key, rel=rel, en=en, note=note, line=line, max_len=MOD_MAX_LEN.get(key)))
    return out


# --------------------------------------------------------------------------------------------
# export
# --------------------------------------------------------------------------------------------
CONSOLE_RE = re.compile(r'memory\s?card|MEMORY\s?CARD|DUALSHOCK|Memory\s?Card|PS2|\bSlot\s?A\b|Nintendo|GameCube|Xbox|console|analog\s?controller|controller port|Controller Socket', re.I)
PLACEHOLDER_TEXTS = ('Auto-complete my quest?', 'Commencement Dialogue.', 'Intermediate Dialogue.')   # ASpeechActivity.cpp:791-793


def subtitle_lang_dir(lang):
    return {'eng': 'ENG', 'eng-uk': 'GBR', 'ger': 'DEU', 'spa': 'ESL', 'fre': 'FRE', 'ita': 'ITA', 'jpn': 'JPN', 'nld': 'NLD'}.get(lang, lang.upper())


def load_base(data_dir, base='eng'):
    loc = parse_locale(TRB(read_file(os.path.join(data_dir, 'Locale', base + '.trb'))))
    subs = {}
    for lib in SUB_LIBS:
        p = os.path.join(data_dir, 'Subs', subtitle_lang_dir(base), lib + '.trb')
        subs[lib] = parse_subs(TRB(read_file(p)))
    return loc, subs


def cmd_export(args):
    data, src, work = args.data, args.src, args.work
    eng, subs = load_base(data, 'eng')
    try:
        uk, subs_uk = load_base(data, 'eng-uk')
    except OSError:
        uk, subs_uk = None, {}
    print('reading the engine source ...')
    refs, limits = scan_source(src)
    print('reading the game data ...')
    dctx, speech_ids = scan_data(data, eng)
    entries = []

    def ref_text(r):
        rel, ln, func, how = r
        area = AREA_NAMES.get(rel.split('/')[0], '')
        return '%s%s:%d%s' % ('%s, ' % area if area else '', rel, ln, ' %s' % func if func else '')

    for i, en in enumerate(eng):
        parts = []
        for c in dctx.get(i, []):
            if c not in parts:
                parts.append(c)
        rs = refs.get(i, [])
        if rs:
            parts.append('code: ' + '; '.join(ref_text(r) for r in rs[:4]) + (' (+%d more)' % (len(rs) - 4) if len(rs) > 4 else ''))
        if not dctx.get(i) and not rs:
            parts.append('no direct reference found (unused, or the code computes the id: the table is grouped by topic, see the neighbouring ids)')
        if re.fullmatch(r"[\d.]+|'", en):
            parts.append('a number/unit symbol (unit conversion data): keep it unchanged')
        if CONSOLE_RE.search(en):
            parts.append('console (PS2/GameCube) system message, probably never shown on PC')
        if en in PLACEHOLDER_TEXTS:
            parts.append('quest placeholder text: the engine compares the English text to add the quest name (ASpeechActivity.cpp:791); keep it untranslated unless that code is changed')
        if '\u00a0' in en:
            parts.append('has no-break spaces (U+00A0) that keep words on one line')
        if uk is not None and uk[i] != en:
            parts.append('UK English version: %s' % json.dumps(uk[i], ensure_ascii=False))
        if not en:
            parts.append('empty string')
        entries.append(dict(id='locale:%04d' % i, source='locale', en=en, context=parts, max_len=None, codes=codes_of(en)))
    # length limits: code buffers (scan_source) and the speech text buffer
    for i, e in enumerate(entries):
        lim = None
        kinds = []
        for n, kind, where in limits.get(i, []):
            if lim is None or n < lim:
                lim = n
            kinds.append('%s after %d chars (%s)' % ('overflows' if kind == 'overflow' else 'cut', n, where))
        if i in speech_ids:
            if lim is None or SPEECH_TEXT_MAX < lim:
                lim = SPEECH_TEXT_MAX
            kinds.append('speech text: the expanded text must fit %d chars (ASpeechActivity.h:46), "%%" only as %%c_..[..] or %%player_name style tags' % SPEECH_TEXT_MAX)
        if kinds:
            e['context'].append('length: ' + '; '.join(kinds) + ' (tags count as their replacement, about %d chars)' % TAG_ALLOWANCE)
        e['max_len'] = lim
    for e in entries:
        e['context'] = ' | '.join(e['context'])

    for lib in SUB_LIBS:
        version, ents = subs[lib]
        uk_map = {n: l for n, l in subs_uk.get(lib, (None, []))[1]} if subs_uk else {}
        for name, lines in ents:
            spk = re.split(r'[_\d]', name)[0].lower()
            who = {'b': 'Bill (bully)', 'g': 'the gopher', 'bs': 'Boss? (bs_)', 'mrs': 'Mrs. Beady', 'mr': 'Mr. Beady',
                   'start': 'intro scene', 'unused': 'unused line'}.get(spk, spk.capitalize())
            for k, (st, en_ms, text) in enumerate(lines):
                where = {'lib_sseqs': 'cut-scene subtitle', 'lib_sim': 'subtitle of a reaction in the farm world',
                         'lib_misc': 'subtitle of a voice line'}[lib]
                c = '%s "%s"%s, line %d/%d, on screen %.1f-%.1f s (%.1f s): bottom of the screen, Rekord18, wraps to the screen width' % (
                    where, name, '' if lib == 'lib_sseqs' else ' (speaker: %s)' % who, k + 1, len(lines), st / 1000.0, en_ms / 1000.0, (en_ms - st) / 1000.0)
                ul = uk_map.get(name)
                if ul and k < len(ul) and ul[k][2] != text:
                    c += ' | UK English version: %s' % json.dumps(ul[k][2], ensure_ascii=False)
                if not text:
                    c += ' | empty line'
                entries.append(dict(id='subs:%s:%s:%d' % (lib, name, k), source='subs/' + lib, en=text, context=c,
                                    max_len=None, codes=codes_of(text, plain=True)))
    mods = []
    for cs in scan_code_strings(src):
        c = 'hardcoded English in the C++ code (OpenBarnyard/Source/%s:%s), not in the locale table: a code change has to show the Hungarian text | %s' % (
            cs['rel'], cs['line'] if cs['line'] else '?', cs['note'])
        if not cs['line']:
            c += ' | NOT FOUND in the source any more'
        mods.append(dict(id='mod:' + cs['key'], source='mod', en=cs['en'], context=c, max_len=cs['max_len'], codes=codes_of(cs['en'])))

    os.makedirs(work, exist_ok=True)
    out_json = os.path.join(work, 'en_strings.json')
    write_text(out_json, json.dumps(entries, ensure_ascii=False, indent=1))
    write_text(os.path.join(work, 'mod_strings_en.json'), json.dumps(mods, ensure_ascii=False, indent=1))

    def esc(s):
        return (s or '').replace('\\', '\\\\').replace('\t', '\\t').replace('\r', '\\r').replace('\n', '\\n')
    tsv = ['id\tcontext\ten'] + ['%s\t%s\t%s' % (e['id'], esc(e['context']), esc(e['en'])) for e in entries + mods]
    write_text(os.path.join(work, 'en_strings.tsv'), '\n'.join(tsv) + '\n')
    stats = collections.OrderedDict()
    for e in entries + mods:
        k = e['source']
        s = stats.setdefault(k, dict(entries=0, chars=0, words=0, unique=set(), with_codes=0))
        s['entries'] += 1
        s['chars'] += len(e['en'] or '')
        s['words'] += len(re.findall(r'[^\W\d_]+(?:\'[^\W\d_]+)?', CODE_RE.sub(' ', e['en'] or '')))
        s['unique'].add(e['en'])
        s['with_codes'] += bool(e['codes'])
    print('wrote %s (%d entries), mod_strings_en.json (%d entries) and en_strings.tsv (both)' % (out_json, len(entries), len(mods)))
    for k, s in stats.items():
        print('  %-16s %5d entries %7d chars %6d words %5d unique texts %4d with codes' % (k, s['entries'], s['chars'], s['words'], len(s['unique']), s['with_codes']))
    print('  speech ids %d, ids with a code reference %d, ids with a data reference %d, ids with a length limit %d' % (
        len(speech_ids), sum(1 for i in range(len(eng)) if refs.get(i)), sum(1 for i in range(len(eng)) if dctx.get(i)),
        sum(1 for e in entries if e['max_len'] is not None)))
    return 0


# --------------------------------------------------------------------------------------------
# translations in
# --------------------------------------------------------------------------------------------
def load_translation(paths):
    """{id: text} merged from one or more files (hu_strings_part01.json, ...). Accepts {id: text},
    [{id, hu|text|translation}], or a .md with ```json blocks."""
    if isinstance(paths, str):
        paths = [paths]
    out = collections.OrderedDict()
    for path in paths:
        for k, v in _load_translation_file(path).items():
            if k in out and out[k] != v:
                print('WARN %s: %s is in more than one input file, the last one wins' % (path, k))
            out[k] = v
    return out


def _load_translation_file(path):
    raw = read_file(path).decode('utf-8-sig')
    blobs = []
    if path.lower().endswith('.md'):
        blobs = re.findall(r'```(?:json)?\s*\n(.*?)\n```', raw, re.S)
        if not blobs:
            raise ValueError('%s: no ```json block' % path)
    else:
        blobs = [raw]
    out = collections.OrderedDict()
    for b in blobs:
        obj = json.loads(b)
        if isinstance(obj, dict):
            if 'entries' in obj and isinstance(obj['entries'], list):
                obj = obj['entries']
            else:
                for k, v in obj.items():
                    out[k] = v
                continue
        for e in obj:
            for k in ('hu', 'text', 'translation', 'tr'):
                if k in e:
                    out[e['id']] = e[k]
                    break
    return out


def load_en(work):
    """en_strings.json + mod_strings_en.json (if there is one)."""
    en = json.loads(read_file(os.path.join(work, 'en_strings.json')).decode('utf-8'))
    p = os.path.join(work, 'mod_strings_en.json')
    if os.path.exists(p):
        en += json.loads(read_file(p).decode('utf-8'))
    return en


def normalize_text(s, punct=True, fallback=False):
    changed = collections.Counter()
    out = []
    for ch in s:
        rep = PUNCT_MAP.get(ch) if punct else None
        if rep is None and fallback:
            rep = FALLBACK_MAP.get(ch)
        if rep is not None:
            changed[ch] += 1
            out.append(rep)
        else:
            out.append(ch)
    return ''.join(out), changed


def build_language(data_dir, tr, normalize=True, fallback=False, base='eng'):
    """Returns (locale bytes, {lib: subtitle bytes}, code strings, report)."""
    eng, subs = load_base(data_dir, base)
    rep = dict(missing=[], unknown=[], normalized=collections.Counter(), used=0)
    known = set()
    loc = []
    for i, en in enumerate(eng):
        k = 'locale:%04d' % i
        known.add(k)
        if k in tr and tr[k] is not None:
            t, ch = normalize_text(str(tr[k]), normalize, fallback)
            rep['normalized'].update(ch)
            loc.append(t)
            rep['used'] += 1
        else:
            loc.append(en)
            rep['missing'].append(k)
    sub_out = {}
    for lib in SUB_LIBS:
        version, ents = subs[lib]
        new = []
        for name, lines in ents:
            nl = []
            for k, (st, en_ms, text) in enumerate(lines):
                key = 'subs:%s:%s:%d' % (lib, name, k)
                known.add(key)
                if key in tr and tr[key] is not None:
                    t, ch = normalize_text(str(tr[key]), normalize, fallback)
                    rep['normalized'].update(ch)
                    nl.append((st, en_ms, t))
                    rep['used'] += 1
                else:
                    nl.append((st, en_ms, text))
                    rep['missing'].append(key)
            new.append((name, nl))
        sub_out[lib] = (version, new)
    code = []
    for cs in CODE_STRINGS:
        key = 'mod:' + cs[0]
        known.add(key)
        hu = tr.get(key)
        if hu is None:
            rep['missing'].append(key)
        else:
            rep['used'] += 1
            hu, ch = normalize_text(str(hu), normalize, fallback)
            rep['normalized'].update(ch)
        code.append(dict(id=key, file='OpenBarnyard/Source/' + cs[1], en=cs[2], hu=hu if hu is not None else cs[2], translated=hu is not None))
    rep['unknown'] = [k for k in tr if k not in known]
    loc_bytes = build_locale(loc)
    sub_bytes = {lib: build_subs(v, e) for lib, (v, e) in sub_out.items()}
    # read back what was written: with the tool's reader and with the simulation of the engine's loader
    back = parse_locale(TRB(loc_bytes))
    assert back == loc, 'locale read back differs'
    mem, eback = engine_locale(loc_bytes)
    assert eback == loc, 'locale read back like the engine differs'
    assert mem == TRB(loc_bytes).memory_size()
    for lib, b in sub_bytes.items():
        assert parse_subs(TRB(b)) == sub_out[lib], 'subtitles read back differ: ' + lib
        for name, lines in sub_out[lib][1][:3] + sub_out[lib][1][-3:]:
            assert engine_subtitle(b, name) == lines, 'subtitle %s read like the engine differs' % name
    rep['memory'] = mem
    return loc_bytes, sub_bytes, code, rep


def cmd_import(args):
    tr = load_translation(args.inp)
    loc_bytes, sub_bytes, code, rep = build_language(args.data, tr, normalize=not args.no_normalize, fallback=args.fallback_chars, base=args.base)
    out = args.out
    if rep['memory'] > LOCALE_BUFFER and not args.force:
        print('ERROR: %s.trb needs %d bytes of the %d byte locale buffer (ALocaleManager.cpp:14), the engine would overflow it; shorten the texts (--force writes it anyway)' % (
            args.lang, rep['memory'], LOCALE_BUFFER))
        return 2
    lp = os.path.join(out, 'Data', 'Locale', args.lang + '.trb')
    write_file(lp, loc_bytes)
    sd = subtitle_lang_dir(args.lang)
    for lib, b in sub_bytes.items():
        write_file(os.path.join(out, 'Data', 'Subs', sd, lib + '.trb'), b)
    write_text(os.path.join(out, 'mod_strings_%s.json' % args.lang), json.dumps(code, ensure_ascii=False, indent=1))
    write_text(os.path.join(out, 'missing_%s.txt' % args.lang), '\n'.join(rep['missing']) + ('\n' if rep['missing'] else ''))
    print('wrote %s (%d bytes, %d of the %d byte locale buffer = %.1f%%, %d bytes left)' % (
        lp, len(loc_bytes), rep['memory'], LOCALE_BUFFER, 100.0 * rep['memory'] / LOCALE_BUFFER, LOCALE_BUFFER - rep['memory']))
    print('wrote %s\\{%s}.trb, mod_strings_%s.json (texts of the C++ code), missing_%s.txt' % (
        os.path.join(out, 'Data', 'Subs', sd), ','.join(SUB_LIBS), args.lang, args.lang))
    print('translated %d texts, %d fell back to English (listed in missing_%s.txt), %d ids of the input are unknown%s' % (
        rep['used'], len(rep['missing']), args.lang, len(rep['unknown']), (': ' + ', '.join(rep['unknown'][:10])) if rep['unknown'] else ''))
    if rep['normalized']:
        print('replaced characters the fonts lack: ' + ', '.join('%s U+%04X x%d' % (c, ord(c), n) for c, n in rep['normalized'].most_common()))
    return 0


# --------------------------------------------------------------------------------------------
# pseudo translation
# --------------------------------------------------------------------------------------------
ACCENT = {'a': 'á', 'e': 'é', 'i': 'í', 'A': 'Á', 'E': 'É', 'I': 'Í'}
ROTATE = {'o': 'óöő', 'u': 'úüű', 'O': 'ÓÖŐ', 'U': 'ÚÜŰ'}


def pseudo_text(text):
    if not text:
        return text
    pieces = split_codes(text)
    visible = sum(len(p) for c, p in pieces if not c)
    if visible == 0 or not any(ch.isalpha() for c, p in pieces if not c for ch in p):
        return text                                    # only codes / numbers: nothing to translate
    target = int(math.ceil(visible * 1.2))
    out = ['ŐÜ ']
    rot = 0
    for is_code, p in pieces:
        if is_code:
            out.append(p)
            continue
        q = []
        for ch in p:
            if ch in ROTATE:
                q.append(ROTATE[ch][rot % 3])
                rot += 1
            else:
                q.append(ACCENT.get(ch, ch))
        out.append(''.join(q))
    now = sum(len(p) for c, p in split_codes(''.join(out)) if not c)
    if now < target:
        need = target - now
        fill = (' ' + HU_LETTERS * 8)[:need] if need >= 2 else HU_LETTERS[:need]
        # before the trailing codes so a closing "%c_..[..]" or "\n" stays last
        k = len(out)
        while k > 0 and split_codes(out[k - 1]) and split_codes(out[k - 1])[0][0]:
            k -= 1
        out.insert(k, fill)
    return ''.join(out)


def cmd_pseudo(args):
    en = load_en(args.work)
    res = collections.OrderedDict((e['id'], pseudo_text(e['en'])) for e in en)
    write_text(args.out, json.dumps(res, ensure_ascii=False, indent=1))
    ven = sum(len(e['en'] or '') for e in en)
    vps = sum(len(v or '') for v in res.values())
    print('wrote %s: %d texts, %d -> %d chars (+%.1f%%), the 18 letters %s' % (
        args.out, len(res), ven, vps, 100.0 * (vps - ven) / ven, 'all used' if all(any(c in v for v in res.values() if v) for c in HU_LETTERS) else 'NOT all used'))
    return 0


# --------------------------------------------------------------------------------------------
# check
# --------------------------------------------------------------------------------------------
def is_not_translated(e):
    """Texts the PC game never shows, or that stay English (CHATGPT_FORDITAS.md, 2026-11-16): lib_misc subtitles
    (never loaded on PC), music licensing 23-68, staff titles 1469-1547, PS2/GameCube-only 1548-1674."""
    if e['source'] == 'subs/lib_misc':
        return True
    if e['source'] == 'locale':
        n = int(e['id'].split(':')[1])
        return 23 <= n <= 68 or 1469 <= n <= 1674
    return False


def cmd_check(args):
    en = load_en(args.work)
    tr = load_translation(args.file)
    fonts = parse_fonts(TRB(read_file(args.fonts)))
    extra = set(HU_LETTERS) if not args.no_assume_glyphs else set()
    drawable = {n: f['chars'] | extra | {'\n', '\r'} for n, f in fonts.items()}
    errors = collections.defaultdict(list)
    warns = collections.defaultdict(list)
    bad_chars = collections.defaultdict(lambda: [0, [], set()])        # new in the translation
    old_chars = collections.defaultdict(lambda: [0, [], set()])        # the English text of the same id has it too
    en_by_id = {e['id']: e for e in en}
    for e in en:
        k = e['id']
        if k not in tr or tr[k] is None:
            if is_not_translated(e):
                warns['not translated on purpose (stays English)'].append(k)
            else:
                errors['missing'].append(k)
            continue
        t = str(tr[k])
        if '\0' in t:
            errors['nul'].append(k)
        c_en, c_tr = e['codes'], codes_of(t, plain=e['source'].startswith('subs/'))
        if collections.Counter(c_en) != collections.Counter(c_tr):
            miss = list((collections.Counter(c_en) - collections.Counter(c_tr)).elements())
            more = list((collections.Counter(c_tr) - collections.Counter(c_en)).elements())
            errors['codes'].append('%s: missing %s, extra %s' % (k, miss, more))
        else:
            p_en = [c for c in c_en if code_kind(c) == 'printf']
            p_tr = [c for c in c_tr if code_kind(c) == 'printf']
            if p_en != p_tr:
                errors['printf order'].append('%s: %s -> %s' % (k, p_en, p_tr))
            elif c_en != c_tr:
                warns['code order'].append('%s: %s -> %s (allowed: %%N, %%c_ and speech tags are found by name)' % (k, c_en, c_tr))
        if e['max_len'] is not None:
            n = expanded_len(t)
            limit = max(e['max_len'], expanded_len(e['en']))     # the English fits, whatever the tags expand to
            if n > limit:
                (errors if 'speech text' in e['context'] or 'overflows' in e['context'] else warns)['max_len'].append(
                    '%s: %d > %d (English %d)' % (k, n, e['max_len'], expanded_len(e['en'])))
        if 'speech text' in (e['context'] or ''):
            rest = CODE_RE.sub(lambda m: '' if code_kind(m.group()) in ('input', 'speech', 'break') else m.group(), t)
            if '%' in rest:
                errors['speech %'].append('%s: a "%%" that is not a tag breaks ASpeechActivity::FormatText' % k)
        for ch in set(t):
            missing_in = [n for n, d in drawable.items() if ch not in d]
            if missing_in:
                b = (old_chars if ch in (e['en'] or '') else bad_chars)[ch]
                b[0] += t.count(ch)
                if len(b[1]) < 5:
                    b[1].append(k)
                b[2].update(missing_in)
    known = set(en_by_id)
    unknown = [k for k in tr if k not in known]
    if unknown:
        warns['unknown ids'] = unknown
    # the table has to fit the locale buffer
    loc = []
    for e in en:
        if e['source'] == 'locale':
            v = tr.get(e['id'])
            loc.append(str(v) if v is not None else e['en'])
    mem = TRB(build_locale(loc)).memory_size()
    if mem > LOCALE_BUFFER:
        errors['locale buffer'].append('%d bytes > %d (ALocaleManager.cpp:14)' % (mem, LOCALE_BUFFER))
    ven = sum(len(e['en'] or '') for e in en)
    vtr = sum(len(str(tr.get(e['id']) if tr.get(e['id']) is not None else e['en'])) for e in en)
    print('checked %d texts of %s against en_strings.json + mod_strings_en.json in %s' % (len(en), ', '.join(args.file), args.work))
    print('  length %d -> %d chars (%+.1f%%), locale table %d of %d bytes (%.1f%%, %d left)' % (
        ven, vtr, 100.0 * (vtr - ven) / ven, mem, LOCALE_BUFFER, 100.0 * mem / LOCALE_BUFFER, LOCALE_BUFFER - mem))
    print('  fonts %s%s' % (', '.join('%s %d chars' % (n, len(f['chars'])) for n, f in fonts.items()),
                            ' + the 18 Hungarian letters assumed (glyph fix)' if extra else ''))
    for title, d in (('ERROR', errors), ('WARN', warns)):
        for kind, items in d.items():
            print('  %s %s: %d' % (title, kind, len(items)))
            for x in items[:args.show]:
                print('      ' + x)
    for title, table in (('ERROR characters the fonts can not draw (shown as nothing)', bad_chars),
                         ('WARN characters the fonts can not draw, but the English text of the same id has them too', old_chars)):
        if table:
            print('  %s: %d kinds' % (title, len(table)))
            for ch, (n, ids, fs) in sorted(table.items(), key=lambda x: -x[1][0]):
                print('      %r U+%04X x%d missing in %s, e.g. %s%s' % (ch, ord(ch), n, '+'.join(sorted(fs)), ', '.join(ids),
                                                                  (' -> import writes %r' % PUNCT_MAP[ch]) if ch in PUNCT_MAP else ''))
    n_err = sum(len(v) for v in errors.values()) + len(bad_chars)
    n_warn = sum(len(v) for v in warns.values()) + len(old_chars)
    print('  result: %s (%d errors, %d warnings)' % ('OK' if not n_err else 'FAILED', n_err, n_warn))
    if args.report:
        chars = lambda t: {c: dict(count=v[0], ids=v[1], fonts=sorted(v[2])) for c, v in t.items()}
        write_text(args.report, json.dumps(dict(errors=errors, warnings=warns, chars=chars(bad_chars), chars_in_english=chars(old_chars),
                                                memory=mem, length=[ven, vtr]), ensure_ascii=False, indent=1))
    return 1 if n_err else 0


# --------------------------------------------------------------------------------------------
# round trip: English through export + import must give the retail files back
# --------------------------------------------------------------------------------------------
def cmd_roundtrip(args):
    en = load_en(args.work)
    tr = collections.OrderedDict((e['id'], e['en']) for e in en)
    path = os.path.join(args.work, 'roundtrip', 'en_as_translation.json')
    write_text(path, json.dumps(tr, ensure_ascii=False, indent=1))
    tr = load_translation(path)
    if not args.keep:
        os.remove(path)
        try:
            os.rmdir(os.path.dirname(path))
        except OSError:
            pass
    loc_bytes, sub_bytes, code, rep = build_language(args.data, tr, normalize=False)
    ok = True
    orig = read_file(os.path.join(args.data, 'Locale', 'eng.trb'))
    same = orig == loc_bytes
    ok &= same
    print('locale: eng.trb %d bytes, rebuilt from en_strings.json %d bytes: %s' % (len(orig), len(loc_bytes), 'byte-identical' if same else 'DIFFERENT'))
    for lib in SUB_LIBS:
        o = read_file(os.path.join(args.data, 'Subs', 'ENG', lib + '.trb'))
        same = o == sub_bytes[lib]
        ok &= same
        print('subs:   ENG/%s.trb %d bytes, rebuilt %d bytes: %s' % (lib, len(o), len(sub_bytes[lib]), 'byte-identical' if same else 'DIFFERENT'))
    # every retail language: read -> write gives the same bytes
    for f in sorted(os.listdir(os.path.join(args.data, 'Locale'))):
        if f.endswith('.trb'):
            d = read_file(os.path.join(args.data, 'Locale', f))
            strings = parse_locale(TRB(d))
            same = build_locale(strings) == d
            mem, eng_view = engine_locale(d)
            same_view = eng_view == strings
            ok &= same and same_view
            print('        Locale/%s read+write: %s, engine-style load: %s, %d bytes of the %d byte buffer' % (
                f, 'identical' if same else 'DIFFERENT', 'same strings' if same_view else 'DIFFERENT', mem, LOCALE_BUFFER))
    sd = os.path.join(args.data, 'Subs')
    n_same = n_all = 0
    for lang in sorted(os.listdir(sd)):
        for lib in SUB_LIBS:
            p = os.path.join(sd, lang, lib + '.trb')
            if os.path.exists(p):
                d = read_file(p)
                v, e = parse_subs(TRB(d))
                n_all += 1
                n_same += build_subs(v, e) == d
    ok &= n_same == n_all
    print('        Subs/*/*.trb read+write: %d of %d identical' % (n_same, n_all))
    print('round trip %s' % ('OK' if ok else 'FAILED'))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', default=DEF_DATA, help='Game\\Data folder (read only)')
    ap.add_argument('--src', default=DEF_SRC, help='OpenBarnyard source tree (read only)')
    ap.add_argument('--work', default=DEF_WORK, help='folder of en_strings.json')
    sp = ap.add_subparsers(dest='cmd', required=True)
    sp.add_parser('export', help='write en_strings.json, mod_strings_en.json, en_strings.tsv')
    p = sp.add_parser('import', help='build <lang>.trb + subtitles from a translation')
    p.add_argument('--lang', default='hun')
    p.add_argument('--in', dest='inp', required=True, nargs='+',
                   help='json {id: text} files (parts are merged; a list of {id, hu} or a .md with ```json blocks also works)')
    p.add_argument('--out', required=True)
    p.add_argument('--base', default='eng-uk', help='English the missing ids fall back to (eng-uk: what the PC game shows; or eng)')
    p.add_argument('--no-normalize', action='store_true', help='keep „ ” – … etc. (the fonts can not draw them)')
    p.add_argument('--fallback-chars', action='store_true', help='write ő ű Ő Ű as õ û Õ Û (if the fonts have no double acute letters)')
    p.add_argument('--force', action='store_true', help='write even if the table overflows the locale buffer')
    p = sp.add_parser('pseudo', help='write a pseudo translation for tests')
    p.add_argument('--out', required=True)
    p = sp.add_parser('check', help='validate a translation against en_strings.json + mod_strings_en.json')
    p.add_argument('file', nargs='+', help='json {id: text} files (parts are merged)')
    p.add_argument('--fonts', default=os.path.join(DEF_DATA, 'GUI', 'fonts.trb'))
    p.add_argument('--no-assume-glyphs', action='store_true', help='do not assume the 18 Hungarian letters were added to the fonts')
    p.add_argument('--show', type=int, default=8, help='examples per problem kind')
    p.add_argument('--report', help='also write the result as json')
    p = sp.add_parser('roundtrip', help='English through en_strings.json + import -> compare with the retail files')
    p.add_argument('--keep', action='store_true', help='keep forditas\\roundtrip\\en_as_translation.json')
    args = ap.parse_args(argv)
    return {'export': cmd_export, 'import': cmd_import, 'pseudo': cmd_pseudo, 'check': cmd_check, 'roundtrip': cmd_roundtrip}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
