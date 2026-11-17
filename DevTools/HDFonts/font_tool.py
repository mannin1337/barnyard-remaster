#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""font_tool.py - what the Barnyard (2006) GUI fonts can draw, and how Hungarian Ő ő Ű ű can be drawn with them.
Python 3.10+ and Pillow, nothing else.

    set PYTHONIOENCODING=utf-8
    py -B font_tool.py [--data <game>\Data] [--out <dir>] <command>

    extract     reads Data\GUI\fonts.trb, Data\GUI\texsec.trb, Data\Matlibs\gui_Fonts.ttl and the HD replacements
                Data\Textures\Fonts\*.tga once; writes <out>\fonts.json (font definitions, every glyph, the texture
                sections cut from the font atlases) and <out>\atlas\*.png (decoded atlases, SD and HD)
    coverage    code point table of every font, the 18 Hungarian accented letters
    sheets      close-ups: Ö Ü Ó Ú ´ ¨ and the substitutes of the missing letters, SD and HD -> glyphs_*.png
    specimen    every code point of every font as the game draws it -> specimen_<font>.png
    composites  the draw-time composites (solution a): composites.json + the C++ tables in composites_cpp.txt
    mockups     the solutions drawn with the engine's own layout rules -> mockup_*.png, closeup_*.png
    context     solution a in a 1080p frame capture of the game (--frame <png>) -> context_1080p_hd.png

Nothing is written outside --out; the game files are only read, each once, by 'extract'.

Layout = Toshi/Source/GUI/T2GUIFont.cpp, DrawTextSingleLine (PC):
    x0 = pen + iLeftPadding;  y0 = lineHeight + iYOffset - charHeight;  quad (charWidth + 1) x (charHeight + 1) texels,
    UV = texel / original texture size (the HD replacement keeps the original size, so glyph rectangles stay in
    original texels);  pen = x0 + iLetterSpacing + iRightPadding + charWidth.   A character without data is skipped.
"""
import argparse
import io
import json
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

DEF_DATA = os.path.join("Game", "Data")
DEF_OUT = os.path.dirname(os.path.abspath(__file__))
U = struct.unpack_from

HU_LETTERS = "ÁÉÍÓÖŐÚÜŰáéíóöőúüű"
NEW_LETTERS = "ŐőŰű"


def log(*a):
    print(*a, flush=True)


# ---------------------------------------------------------------------------------------------------------------------
# TRB (TOSHI 2.0 little endian): 'TSFL' size 'TRBF', hunks HDRX, SECT (or BTEC packed SECC), RELC, SYMB
# ---------------------------------------------------------------------------------------------------------------------
def btec_decompress(d, pos):
    magic, ver, csize, size = U("<4sIII", d, pos)
    if magic != b"BTEC":
        raise ValueError("no BTEC header")
    pos += 16
    xor = None
    if ver == 0x00010003:
        xor = d[pos]
        pos += 4
    elif ver != 0x00010002:
        raise ValueError("BTEC version %#x" % ver)
    out = bytearray()
    end = pos + csize
    while pos < end:
        b = d[pos]
        pos += 1
        n = b & 0x3F
        if b & 0x40:
            n = (n << 8) | d[pos]
            pos += 1
        n += 1
        if b & 0x80:
            out += d[pos:pos + n]
            pos += n
        else:
            off = d[pos]
            pos += 1
            if off & 0x80:
                off = ((off & 0x7F) << 8) | d[pos]
                pos += 1
            off += 1
            s = len(out) - off
            if off >= n:
                out += out[s:s + n]
            else:
                out += (bytes(out[s:s + off]) * (n // off + 1))[:n]
    if xor is not None:
        out = bytearray(v ^ xor for v in out)
    return bytes(out), end


class TRB:
    def __init__(self, d):
        if d[:4] != b"TSFL" or d[8:12] != b"TRBF":
            raise ValueError("not a little endian TRB")
        hunks = {}
        p = 12
        while p + 8 <= len(d):
            size = U("<I", d, p + 4)[0]
            hunks[d[p:p + 4]] = p + 8
            p += 8 + ((size + 3) & ~3)
        ho = hunks[b"HDRX"]
        sizes = [U("<I", d, ho + 12 + i * 16)[0] for i in range(U("<I", d, ho + 4)[0])]
        self.secs = []
        if b"SECT" in hunks:
            cur = hunks[b"SECT"]
            for s in sizes:
                self.secs.append(d[cur:cur + s])
                cur += s
        else:
            cur = hunks[b"SECC"]
            for s in sizes:
                data, cur = btec_decompress(d, cur)
                self.secs.append(data)
        self.rel = {}
        if b"RELC" in hunks:
            ro = hunks[b"RELC"]
            for i in range(U("<I", d, ro)[0]):
                s1, s2, off = U("<HHI", d, ro + 4 + i * 8)
                self.rel[(s1, off)] = s2
        yo = hunks[b"SYMB"]
        n = U("<I", d, yo)[0]
        names = yo + 4 + n * 12
        self.symbols = []
        for i in range(n):
            sec, noff, _pad, _hash, doff = U("<HHHHI", d, yo + 4 + i * 12)
            self.symbols.append((d[names + noff:d.index(b"\0", names + noff)].decode("latin1"), sec, doff))

    def ptr(self, sec, off):
        t = self.rel.get((sec, off))
        return None if t is None else (t, U("<I", self.secs[sec], off)[0])

    def cstr(self, sec, off):
        b = self.secs[sec]
        return b[off:b.index(b"\0", off)].decode("latin1")


def dds_info(dds):
    _size, flags, h, w, _pitch, _depth, mips = U("<7I", dds, 4)
    pf_flags, fourcc, bits, rm, gm, bm, am = U("<I4sIIIII", dds, 80)
    if pf_flags & 0x4:
        fmt = fourcc.decode("latin1")
    else:
        fmt = {(32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000): "A8R8G8B8", (24, 0xFF0000, 0xFF00, 0xFF, 0): "R8G8B8",
               (16, 0xF00, 0xF0, 0xF, 0xF000): "A4R4G4B4"}.get((bits, rm, gm, bm, am if pf_flags & 1 else 0), "?")
    return {"format": fmt, "width": w, "height": h, "mips": mips if (flags & 0x20000) and mips else 1}


def decode_dds(dds):
    info = dds_info(dds)
    w, h = info["width"], info["height"]
    if info["format"] == "A8R8G8B8":
        return Image.frombuffer("RGBA", (w, h), dds[128:128 + w * h * 4], "raw", "BGRA", 0, 1).copy(), info
    if info["format"] == "A4R4G4B4":
        return Image.frombuffer("RGBA", (w, h), dds[128:128 + w * h * 2], "raw", "RGBA;4B", 0, 1).copy(), info
    im = Image.open(io.BytesIO(dds))
    im.load()
    return im.convert("RGBA"), info


def read_once(path):
    with open(path, "rb") as f:
        return f.read()


# ---------------------------------------------------------------------------------------------------------------------
# extract
# ---------------------------------------------------------------------------------------------------------------------
def parse_fonts(trb):
    """T2GUIFontDef (Toshi/Source/GUI/T2GUIFont.h), 28 bytes: void* (-> font name); char* szTextureNames;
    int8 iLetterSpacing, iLinesMargin; uint8 uiLineHeight, uiNumMaterials; uint16 ui16MaxCharacter, ui16MinCharacter;
    int32 Unk3 (= number of characters); int32* pCharactersMap; CharData* pCharactersData.
    CharData, 12 bytes: int8 Unk1, Unk2; uint16 x, y; uint8 w, h; int8 left, right, yoff, Unk6"""
    fonts = []
    for name, sec, off in trb.symbols:
        b = trb.secs[sec]
        _pn, _pt, ls, lm, lh, nmat, mx, mn, nchars, _pm, _pd = U("<IIbbBBHHiII", b, off)
        pname, ptex = trb.ptr(sec, off), trb.ptr(sec, off + 4)
        pmap, pdat = trb.ptr(sec, off + 20), trb.ptr(sec, off + 24)
        textures = []
        t = ptex[1]
        for _ in range(nmat):
            s = trb.cstr(ptex[0], t)
            textures.append(s)
            t += len(s) + 1
        m = U("<%di" % (mx - mn + 1), trb.secs[pmap[0]], pmap[1])
        glyphs = {}
        for i, idx in enumerate(m):
            if idx < 0:
                continue
            u1, u2, x, y, w, h, left, right, yoff, u6 = U("<bbHHBBbbbb", trb.secs[pdat[0]], pdat[1] + idx * 12)
            glyphs[mn + i] = {"index": idx, "x": x, "y": y, "w": w, "h": h, "left": left, "right": right,
                              "yoff": yoff, "unk1": u1, "unk2": u2, "unk6": u6}
        fonts.append({
            "name": name, "name_field": trb.cstr(*pname) if pname else None, "textures": textures,
            "letter_spacing": ls, "lines_margin": lm, "line_height": lh, "num_materials": nmat,
            "min_char": mn, "max_char": mx, "num_chars": nchars,
            "def_offset": off, "map_offset": pmap[1], "data_offset": pdat[1],
            "glyphs": {"%04X" % k: v for k, v in sorted(glyphs.items())},
        })
    return fonts


def parse_texsec(trb, wanted):
    """Texture sections (T2GUITextureSectionManager, 60 bytes each) that are cut from the font textures."""
    out = []
    for sym, sec, off in trb.symbols:
        if not sym.lower().endswith("texturesections"):
            continue
        count = U("<I", trb.secs[sec], off + 4)[0]
        first = trb.ptr(sec, off)
        if first is None:
            continue
        ssec, soff = first
        for i in range(count):
            o = soff + i * 60
            pn, pt = trb.ptr(ssec, o), trb.ptr(ssec, o + 4)
            if pt is None:
                continue
            tex = trb.cstr(*pt)
            if tex.replace("/", "\\").lower() not in wanted:
                continue
            x1, y1, x2, y2 = U("<4I", trb.secs[ssec], o + 8)
            out.append({"name": trb.cstr(*pn) if pn else "?", "texture": tex, "rect": [x1, y1, x2, y2]})
    return out


def cmd_extract(a):
    data, out = a.data, a.out
    os.makedirs(os.path.join(out, "atlas"), exist_ok=True)
    fonts = parse_fonts(TRB(read_once(os.path.join(data, "GUI", "fonts.trb"))))
    tex_names = {t.lower() for f in fonts for t in f["textures"]}
    sections = parse_texsec(TRB(read_once(os.path.join(data, "GUI", "texsec.trb"))), tex_names)
    ttl = TRB(read_once(os.path.join(data, "Matlibs", "gui_Fonts.ttl")))
    atlases = {}
    for sym, sec, off in ttl.symbols:
        if sym.lower() != "ttl" and not sym.lower().endswith("_ttl"):
            continue
        count = U("<i", ttl.secs[sec], off)[0]
        isec, ioff = ttl.ptr(sec, off + 4)
        for i in range(count):
            o = ioff + i * 16
            _fmt, _n, size, _d = U("<IIII", ttl.secs[isec], o)
            pname, pdata = ttl.ptr(isec, o + 4), ttl.ptr(isec, o + 12)
            name = ttl.cstr(*pname)
            im, info = decode_dds(ttl.secs[pdata[0]][pdata[1]:pdata[1] + size])
            stem = os.path.splitext(os.path.basename(name.replace("\\", "/")))[0]
            png = os.path.join("atlas", stem + ".png")
            im.save(os.path.join(out, png))
            entry = {"library": "Matlibs/gui_Fonts.ttl", "info": info, "png": png}
            hd_path = os.path.join(data, "Textures", name.replace("/", "\\"))
            if os.path.isfile(hd_path):
                him, hinfo = decode_dds(read_once(hd_path))
                hpng = os.path.join("atlas", stem + "_hd.png")
                him.save(os.path.join(out, hpng))
                entry["hd"] = {"file": hd_path, "info": hinfo, "png": hpng}
            atlases[name] = entry
            log("texture %-28s %s %dx%d%s" % (name, info["format"], info["width"], info["height"],
                                             "  HD %s %dx%d" % (entry["hd"]["info"]["format"], entry["hd"]["info"]["width"],
                                                                entry["hd"]["info"]["height"]) if "hd" in entry else ""))
    with open(os.path.join(out, "fonts.json"), "w", encoding="utf-8") as f:
        json.dump({"fonts": fonts, "atlases": atlases, "texture_sections": sections}, f, ensure_ascii=False, indent=1)
    for fo in fonts:
        log("font %s: %d glyphs, %s, line height %d, letter spacing %d, lines margin %d, chars %#x-%#x"
            % (fo["name"], len(fo["glyphs"]), fo["textures"], fo["line_height"], fo["letter_spacing"],
               fo["lines_margin"], fo["min_char"], fo["max_char"]))
    for s in sections:
        log("texture section %s in %s at %s" % (s["name"], s["texture"], s["rect"]))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# fonts in memory
# ---------------------------------------------------------------------------------------------------------------------
class Font:
    def __init__(self, fo, atlas_entry, out, hd=False):
        self.fo = fo
        self.name = fo["name"]
        self.g = {int(k, 16): v for k, v in fo["glyphs"].items()}
        self.lh, self.ls, self.lm = fo["line_height"], fo["letter_spacing"], fo["lines_margin"]
        self.base_w, self.base_h = atlas_entry["info"]["width"], atlas_entry["info"]["height"]
        self.sd = Image.open(os.path.join(out, atlas_entry["png"])).convert("RGBA")
        self.hd = Image.open(os.path.join(out, atlas_entry["hd"]["png"])).convert("RGBA") if "hd" in atlas_entry else None
        self.atlas = self.hd if (hd and self.hd is not None) else self.sd
        self.k = self.atlas.width / self.base_w  # texels of the drawn atlas per original texel
        # the GUI materials use wrap addressing: a few icon rectangles of Rekord18 reach past the bottom edge
        w, h = self.atlas.size
        self.wrapped = Image.new("RGBA", (2 * w, 2 * h))
        for ox in (0, w):
            for oy in (0, h):
                self.wrapped.paste(self.atlas, (ox, oy))

    def has(self, c):
        return ord(c) in self.g

    def top(self, g):
        return self.lh + g["yoff"] - g["h"]

    def cell(self, cp):
        """The quad of a glyph as an image of original texels (SD atlas)."""
        g = self.g[cp]
        return self.sd.crop((g["x"], g["y"], g["x"] + g["w"] + 1, g["y"] + g["h"] + 1))


def load(out, hd=False):
    with open(os.path.join(out, "fonts.json"), encoding="utf-8") as f:
        doc = json.load(f)
    return doc, [Font(fo, doc["atlases"][fo["textures"][0]], out, hd) for fo in doc["fonts"]]


def ui_font(size, bold=False):
    for p in ((r"C:\Windows\Fonts\segoeuib.ttf",) if bold else ()) + (r"C:\Windows\Fonts\segoeui.ttf", r"C:\Windows\Fonts\arial.ttf"):
        if os.path.isfile(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


# ---------------------------------------------------------------------------------------------------------------------
# coverage
# ---------------------------------------------------------------------------------------------------------------------
def ranges(cps):
    out = []
    for c in sorted(cps):
        if out and c == out[-1][1] + 1:
            out[-1][1] = c
        else:
            out.append([c, c])
    return out


def cmd_coverage(a):
    _doc, fonts = load(a.out)
    for f in fonts:
        cps = sorted(f.g)
        log("== %s (%s): %d glyphs, line height %d" % (f.name, f.fo["textures"][0], len(cps), f.lh))
        for lo, hi in ranges(cps):
            txt = "".join(chr(c) for c in range(lo, hi + 1)) if lo < 0xF00 else "(controller button icons)"
            log("   U+%04X-U+%04X %3d  %s" % (lo, hi, hi - lo + 1, txt))
        log("   Latin-1 A0-FF missing: %s" % " ".join("%s(%02X)" % (chr(c), c) for c in range(0xA0, 0x100) if c not in f.g))
        miss = [c for c in HU_LETTERS if not f.has(c)]
        log("   Hungarian letters: %d of 18, missing: %s" % (18 - len(miss), " ".join(miss)))
        for c in "´¨˝":
            log("   %s U+%04X: %s" % (c, ord(c), "yes" if f.has(c) else "no"))
        cells = {}
        for cp, g in f.g.items():
            cells.setdefault((g["x"], g["y"]), []).append(cp)
        shared = {k: v for k, v in cells.items() if len(v) > 1}
        log("   %d entries share a cell with another: %s" % (sum(len(v) for v in shared.values()),
            " ".join("".join(chr(c) for c in v) for v in shared.values())))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# glyph close-ups
# ---------------------------------------------------------------------------------------------------------------------
def checker(w, h, s=4, c0=(70, 70, 78, 255), c1=(90, 90, 98, 255)):
    im = Image.new("RGBA", (w, h), c0)
    d = ImageDraw.Draw(im)
    for y in range(0, h, s):
        for x in range(0, w, s):
            if (x // s + y // s) % 2:
                d.rectangle((x, y, x + s - 1, y + s - 1), fill=c1)
    return im


def cmd_sheets(a):
    chars = "ÖöÜüÓóÚú´¨OoUuÕõÛûÔô"
    for hd in (False, True):
        _doc, fonts = load(a.out, hd)
        for f in fonts:
            if hd and f.k == 1:
                continue
            zoom = 8 if not hd else 4
            lf = ui_font(13)
            cells = []
            for c in chars:
                if not f.has(c):
                    cells.append((c, None, None))
                    continue
                g = f.g[ord(c)]
                k = f.k
                im = f.atlas.crop((round(g["x"] * k), round(g["y"] * k), round((g["x"] + g["w"] + 1) * k), round((g["y"] + g["h"] + 1) * k)))
                cells.append((c, im, g))
            cw = max(im.width for _, im, _ in cells if im) * zoom + 16
            ch = max(im.height for _, im, _ in cells if im) * zoom + 52
            cols = 10
            rows = (len(cells) + cols - 1) // cols
            sheet = Image.new("RGBA", (cols * cw, rows * ch + 24), (30, 30, 36, 255))
            d = ImageDraw.Draw(sheet)
            d.text((6, 4), "%s %s atlas (%s), %dx zoom, checker = transparent; numbers: x y w h left right yoff (fonts.trb)"
                   % (f.name, "HD 2x" if hd else "SD", f.fo["textures"][0], zoom), fill=(230, 230, 230), font=lf)
            for i, (c, im, g) in enumerate(cells):
                x0, y0 = (i % cols) * cw, (i // cols) * ch + 24
                d.text((x0 + 6, y0 + 2), "%s U+%04X" % (c, ord(c)), fill=(255, 220, 120), font=lf)
                if im is None:
                    d.text((x0 + 6, y0 + 22), "missing", fill=(255, 90, 90), font=lf)
                    continue
                big = im.resize((im.width * zoom, im.height * zoom), Image.NEAREST)
                bg = checker(big.width, big.height, zoom)
                bg.alpha_composite(big)
                sheet.paste(bg, (x0 + 6, y0 + 20))
                d.text((x0 + 6, y0 + 22 + big.height), "%d %d %d %d %d %d %d" % (g["x"], g["y"], g["w"], g["h"], g["left"], g["right"], g["yoff"]),
                       fill=(170, 170, 170), font=ui_font(11))
            p = os.path.join(a.out, "glyphs_%s%s.png" % (f.name, "_hd" if hd else ""))
            sheet.convert("RGB").save(p)
            log("wrote", p)
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# drawing like T2GUIFont::DrawTextSingleLine
# ---------------------------------------------------------------------------------------------------------------------
class Quad:
    """src: rectangle of original texels; dst: rectangle in font pixels (x relative to the pen start, y to the line top)."""
    __slots__ = ("src", "dst", "img")

    def __init__(self, src, dst, img=None):
        self.src, self.dst, self.img = src, dst, img


def glyph_quads(f, cp, pen, mode):
    """Quads of one character and the new pen position. mode: dict with 'remap' {cp: cp}, 'alias' {cp: cp},
    'composites' {cp: spec}, 'images' {cp: (image of original texels, x, y)}."""
    cp = mode.get("remap", {}).get(cp, cp)
    comp = mode.get("composites", {}).get(cp)
    look = comp["alias"] if comp else mode.get("alias", {}).get(cp, cp)
    g = f.g.get(look)
    if g is None:
        return [], pen  # FindCharData == TNULL: nothing drawn, no advance
    x0 = pen + g["left"]
    y0 = f.top(g)
    quads = []
    if g["w"] and g["h"]:
        img = mode.get("images", {}).get(cp)
        if img is not None:  # solution b: the glyph's own cell (new entry with the same metrics as the alias)
            quads.append(Quad(None, (x0, y0, g["w"] + 1, g["h"] + 1), img))
        elif comp:
            cut = comp["cut"]
            quads.append(Quad((g["x"], g["y"] + cut, g["w"] + 1, g["h"] + 1 - cut), (x0, y0 + cut, g["w"] + 1, g["h"] + 1 - cut)))
            for p in comp["parts"]:
                sx, sy, sw, sh = p["src"]
                dx, dy, dw, dh = p["dst"]
                quads.append(Quad((sx, sy, sw, sh), (x0 + dx, y0 + dy, dw, dh)))
        else:
            quads.append(Quad((g["x"], g["y"], g["w"] + 1, g["h"] + 1), (x0, y0, g["w"] + 1, g["h"] + 1)))
    return quads, x0 + f.ls + g["right"] + g["w"]


def line_quads(f, text, mode):
    pen, quads = 0, []
    for ch in text:
        q, pen = glyph_quads(f, ord(ch), pen, mode)
        quads += q
    return quads, pen


def tint(im, colour):
    if colour == (255, 255, 255):
        return im
    r, g, b, al = im.split()
    r = r.point(lambda v: v * colour[0] // 255)
    g = g.point(lambda v: v * colour[1] // 255)
    b = b.point(lambda v: v * colour[2] // 255)
    return Image.merge("RGBA", (r, g, b, al))


def render_quads(f, quads, scale, colour=(255, 255, 255), canvas=None, origin=(0, 0)):
    """Draws the quads with bilinear sampling of the atlas (the GUI renderer uses linear filtering): an image or onto
    a canvas at origin (pixels). Returns (image, (x of the pen start, y of the line top) in it)."""
    if canvas is None:
        x1 = min(q.dst[0] for q in quads) if quads else 0
        y1 = min(q.dst[1] for q in quads) if quads else 0
        x2 = max(q.dst[0] + q.dst[2] for q in quads) if quads else 1
        y2 = max(q.dst[1] + q.dst[3] for q in quads) if quads else 1
        canvas = Image.new("RGBA", (int((x2 - x1) * scale) + 4, int((y2 - y1) * scale) + 4), (0, 0, 0, 0))
        origin = (-x1 * scale + 2, -y1 * scale + 2)
    k = f.k
    for q in quads:
        dx, dy, dw, dh = q.dst
        size = (max(1, round(dw * scale)), max(1, round(dh * scale)))
        if q.img is not None:
            img, ik = q.img
            tile = img.resize(size, Image.BILINEAR)
        else:
            sx, sy, sw, sh = q.src
            tile = f.wrapped.resize(size, Image.BILINEAR, box=(sx * k, sy * k, (sx + sw) * k, (sy + sh) * k))
        tile = tint(tile, colour)
        layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
        layer.paste(tile, (round(origin[0] + dx * scale), round(origin[1] + dy * scale)))
        canvas.alpha_composite(layer)
    return canvas, origin


# ---------------------------------------------------------------------------------------------------------------------
# solution a: composites, computed from the atlas
# ---------------------------------------------------------------------------------------------------------------------
# The upper case Ñ Ò Ó Ô Õ Ö of Rekord26 point one cell to the left in the retail fonts.trb (Ó draws Ò, Ö draws Õ,
# Ñ a 4 texel sliver); the lower case entries point at the right cells and this all caps font has one design for both.
REMAP_FIX = {"Rekord26": {0xD1: 0xF1, 0xD2: 0xF2, 0xD3: 0xF3, 0xD4: 0xF4, 0xD5: 0xF5, 0xD6: 0xF6}}

# How each new letter is put together:
#   alias   the entry whose metrics (advance, quad) the letter uses: FindCharData returns it for the new code point,
#           so measuring and line breaking stay exactly as for that letter
#   cut     True: the rows of the alias quad above the body are not drawn (the dots of the umlaut letter)
#   dots    the umlaut letter whose two dots give the x centres of the two accents
#   accent  ("of", Ó): the accent of that letter (cut above its body), as far above the body as on that letter;
#           ("glyph", ´): that glyph, at the height of the dots
#   scale   (x, y) size of each accent copy: narrower than high makes the strokes steeper, as a double acute is
RECIPES = {
    # the accented letters of Rekord18 are as big as the plain ones: the body is the plain letter itself
    # (its Ü Ú and ü ú sit 2 and 1 texels below the baseline in the retail data, the plain U and u don't)
    "Rekord18": {
        0x150: dict(alias="O", cut=False, dots="Ö", accent=("of", "Ó"), scale=(0.8, 1.0)),
        0x151: dict(alias="o", cut=False, dots="ö", accent=("of", "ó"), scale=(0.8, 1.0)),
        0x170: dict(alias="U", cut=False, dots="Ü", accent=("of", "Ú"), scale=(0.8, 1.0)),
        0x171: dict(alias="u", cut=False, dots="ü", accent=("of", "ú"), scale=(0.8, 1.0)),
    },
    # the accented letters of Rekord26 are smaller than the plain ones: the body is the umlaut letter without its dots
    "Rekord26": {
        0x150: dict(alias="ö", cut=True, dots="ö", accent=("glyph", "´"), scale=(0.55, 0.9)),
        0x151: dict(alias="ö", cut=True, dots="ö", accent=("glyph", "´"), scale=(0.55, 0.9)),
        0x170: dict(alias="Ü", cut=True, dots="Ü", accent=("glyph", "´"), scale=(0.55, 0.9)),
        0x171: dict(alias="ü", cut=True, dots="ü", accent=("glyph", "´"), scale=(0.55, 0.9)),
    },
}


def fill_components(img):
    """Connected components of the white fill (opaque, light) of a glyph cell, top to bottom."""
    w, h = img.size
    px = img.load()
    fill = [[px[i, j][3] > 140 and sum(px[i, j][:3]) > 450 for i in range(w)] for j in range(h)]
    seen = [[False] * w for _ in range(h)]
    out = []
    for j in range(h):
        for i in range(w):
            if not fill[j][i] or seen[j][i]:
                continue
            st, pts = [(i, j)], []
            seen[j][i] = True
            while st:
                x, y = st.pop()
                pts.append((x, y))
                for p, q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= p < w and 0 <= q < h and fill[q][p] and not seen[q][p]:
                        seen[q][p] = True
                        st.append((p, q))
            if len(pts) < 3:
                continue
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            out.append({"n": len(pts), "x0": min(xs), "y0": min(ys), "x1": max(xs) + 1, "y1": max(ys) + 1,
                        "cx": sum(xs) / len(xs) + 0.5, "cy": sum(ys) / len(ys) + 0.5})
    return sorted(out, key=lambda c: (c["y0"], c["x0"]))


def split_body(comps):
    body = max(comps, key=lambda c: c["n"])
    marks = sorted([c for c in comps if c is not body and c["y1"] <= body["y0"] + 2], key=lambda c: c["cx"])
    return body, marks


def ink_rows(img, thr=8):
    a = img.getchannel("A").load()
    return [any(a[i, j] > thr for i in range(img.width)) for j in range(img.height)]


def design(f):
    """Composite specs of one font: {cp: {alias, cut, parts: [{src, dst}], ...}} in original texels / font pixels."""
    rec = RECIPES[f.name]
    specs = {}
    for cp, r in rec.items():
        alias = ord(r["alias"])
        ga = f.g[alias]
        body_a, _ = split_body(fill_components(f.cell(alias)))
        dots_cp = ord(r["dots"])
        gd = f.g[dots_cp]
        body_d, dots = split_body(fill_components(f.cell(dots_cp)))
        if len(dots) != 2:
            raise ValueError("%s %s: %d dots" % (f.name, r["dots"], len(dots)))
        # x of the dot centres in the alias quad: the bodies are lined up
        shift = (ga["left"] + body_a["cx"]) - (gd["left"] + body_d["cx"])
        centres = [gd["left"] + d["cx"] + shift - ga["left"] for d in dots]
        cut = 0
        if r["cut"]:
            cut = body_a["y0"] - 1  # keep one outline row above the fill of the body
        # the accent: a rectangle of original texels and the centre of its fill in it
        kind, src_ch = r["accent"]
        gs = f.g[ord(src_ch)]
        cimg = f.cell(ord(src_ch))
        comps = fill_components(cimg)
        if kind == "of":
            body_s, marks = split_body(comps)
            acc = marks[0]
            # the accent's outline merges into the body's (no empty row between them in either font): stop two rows
            # above the body's fill, or the copies carry pieces of the body's top outline sideways
            y_lim = body_s["y0"] - 2
            ty = body_a["y0"] - (body_s["y0"] - acc["cy"])  # as far above the body as on the accented letter
        else:
            acc = max(comps, key=lambda c: c["n"])
            y_lim = acc["y1"] + 3
            ty = sum(d["cy"] for d in dots) / 2 + f.top(gd) - f.top(ga)  # the height of the dots
        rows = ink_rows(cimg)
        sx0, sx1 = max(0, acc["x0"] - 3), min(cimg.width, acc["x1"] + 3)
        if kind == "glyph":
            sx1 = min(sx1, acc["x1"] + 3, cimg.width - 2)  # Rekord26 '´' has a neighbour's edge in its last column
        sy0 = max(0, acc["y0"] - 3)
        while sy0 < acc["y0"] and not rows[sy0]:
            sy0 += 1
        sy0 = max(0, sy0 - 1)
        sy1 = min(y_lim, cimg.height)
        scx, scy = r["scale"]
        parts = []
        for tx in centres:
            dw, dh = (sx1 - sx0) * scx, (sy1 - sy0) * scy
            dx = tx - (acc["cx"] - sx0) * scx
            dy = ty - (acc["cy"] - sy0) * scy
            parts.append({"src": [gs["x"] + sx0, gs["y"] + sy0, sx1 - sx0, sy1 - sy0],
                          "dst": [round(dx, 2), round(dy, 2), round(dw, 2), round(dh, 2)]})
        specs[cp] = {"alias": alias, "cut": cut, "parts": parts, "recipe": {k: v for k, v in r.items()}}
    return specs


def compose_offline(f, cp, spec, supersample=4):
    """Solution b: the composite drawn once into a glyph cell (original texels of the drawn atlas, k texels per
    original texel), the shapes merged (lighter colour and the higher alpha win, premultiplied), so overlapping outlines
    don't cut into fills. Returns (image, k)."""
    ga = f.g[spec["alias"]]
    k = f.k
    W, H = round((ga["w"] + 1) * k), round((ga["h"] + 1) * k)
    layers = []
    body = f.atlas.crop((round(ga["x"] * k), round((ga["y"] + spec["cut"]) * k), round((ga["x"] + ga["w"] + 1) * k), round((ga["y"] + ga["h"] + 1) * k)))
    lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    lay.paste(body, (0, round(spec["cut"] * k)))
    layers.append(lay)
    for p in spec["parts"]:
        sx, sy, sw, sh = p["src"]
        dx, dy, dw, dh = p["dst"]
        ss = supersample
        big = f.atlas.resize((max(1, round(dw * k * ss)), max(1, round(dh * k * ss))), Image.BICUBIC,
                             box=(sx * k, sy * k, (sx + sw) * k, (sy + sh) * k))
        lay = Image.new("RGBA", (W * ss, H * ss), (0, 0, 0, 0))
        lay.paste(big, (round(dx * k * ss), round(dy * k * ss)))
        layers.append(lay.resize((W, H), Image.LANCZOS))
    # merge: premultiplied max
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    o = out.load()
    pls = [l.load() for l in layers]
    for y in range(H):
        for x in range(W):
            best_a, best = 0, [0, 0, 0]
            for pl in pls:
                r, g, b, al = pl[x, y]
                if al > best_a:
                    best_a = al
                for i, v in enumerate((r, g, b)):
                    pv = v * al
                    if pv > best[i]:
                        best[i] = pv
            if best_a:
                o[x, y] = (min(255, best[0] // best_a), min(255, best[1] // best_a), min(255, best[2] // best_a), best_a)
    return out, k


def cmd_composites(a):
    _doc, fonts = load(a.out)
    allspecs = {}
    lines = []
    for f in fonts:
        specs = design(f)
        allspecs[f.name] = {"texture": f.fo["textures"][0], "remap_fix": {"%04X" % k: "%04X" % v for k, v in REMAP_FIX.get(f.name, {}).items()},
                            "composites": {"%04X" % cp: s for cp, s in specs.items()}}
        lines.append("// %s (%s): Hungarian letters drawn as <alias entry> (rows from 'cut' down) + 2 accent quads." % (f.name, f.fo["textures"][0]))
        lines.append("// src = rectangle of original texels (x, y, w, h); dst = x, y, w, h in font pixels from the top left of the alias quad")
        for cp, s in specs.items():
            ps = ", ".join("{ %d, %d, %d, %d, %.2ff, %.2ff, %.2ff, %.2ff }" % tuple(p["src"] + p["dst"]) for p in s["parts"])
            lines.append("{ 0x%04X /* %s */, 0x%04X /* %s */, %d, { %s } }," % (cp, chr(cp), s["alias"], chr(s["alias"]), s["cut"], ps))
        if f.name in REMAP_FIX:
            lines.append("// %s retail map fix (pCharactersMap[ upper - min ] = pCharactersMap[ lower - min ]): %s"
                         % (f.name, " ".join("%s->%s" % (chr(k), chr(v)) for k, v in REMAP_FIX[f.name].items())))
        lines.append("")
    with open(os.path.join(a.out, "composites.json"), "w", encoding="utf-8") as fh:
        json.dump(allspecs, fh, ensure_ascii=False, indent=1)
    with open(os.path.join(a.out, "composites_cpp.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    log("\n".join(lines))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# specimen
# ---------------------------------------------------------------------------------------------------------------------
def cmd_specimen(a):
    _doc, fonts = load(a.out)
    lf = ui_font(12)
    for f in fonts:
        cps = [c for c in sorted(f.g) if c < 0xF00] + [c for c in sorted(f.g) if c >= 0xF00]
        scale = 3
        cw, chh = 34 * scale // 2 + 40, (f.lh + 30) * scale
        cols = 16
        rows = (len(cps) + cols - 1) // cols
        sheet = Image.new("RGBA", (cols * cw, rows * chh + 30), (36, 40, 52, 255))
        d = ImageDraw.Draw(sheet)
        d.text((6, 6), "%s: every entry of fonts.trb as T2GUIFont draws it (label = code point and the character it stands for)" % f.name,
               fill=(235, 235, 235), font=lf)
        for i, cp in enumerate(cps):
            x0, y0 = (i % cols) * cw, (i // cols) * chh + 30
            q, _ = glyph_quads(f, cp, 0, {})
            label = "%04X %s" % (cp, chr(cp) if cp < 0xF00 else "icon")
            d.text((x0 + 4, y0 + 2), label, fill=(255, 215, 120), font=lf)
            if q:
                render_quads(f, q, scale, canvas=sheet, origin=(x0 + 8 - q[0].dst[0] * scale + 6, y0 + 18 + 10 * scale))
        p = os.path.join(a.out, "specimen_%s.png" % f.name)
        sheet.convert("RGB").save(p)
        log("wrote", p)
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# mock-ups
# ---------------------------------------------------------------------------------------------------------------------
def latin1_sub(s):
    return s.translate(str.maketrans({"Ő": "Õ", "ő": "õ", "Ű": "Û", "ű": "û"}))


def umlaut_sub(s):
    return s.translate(str.maketrans({"Ő": "Ö", "ő": "ö", "Ű": "Ü", "ű": "ü"}))


def modes_for(f, specs, offline):
    fix = REMAP_FIX.get(f.name, {})
    return [
        ("Original: missing letters skipped" + (", Ó/Ö use the wrong cells" if fix else ""), {}, None),
        ("Umlaut substitution: ő→ö, ű→ü", {"remap": fix}, umlaut_sub),
        ("Latin-1 substitution: ő→õ, ű→û", {"remap": fix}, latin1_sub),
        ("(a) Draw-time composite: base letter and two accents", {"remap": fix, "composites": specs}, None),
        ("(b) Precomposed letter in the atlas", {"remap": fix, "composites": specs, "images": offline}, None),
    ]


SAMPLES = {
    "Rekord18": ["Árvíztűrő tükörfúrógép", "ÁRVÍZTŰRŐ TÜKÖRFÚRÓGÉP", "Győzd le őket, és tűzz el a sűrű erdőbe!"],
    "Rekord26": ["ÓRA, ÖKÖR", "BEÁLLÍTÁSOK", "ŐRÜLT FŰZŐ", "Következő tűzijáték"],
}


def draw_block(f, text, mode, sub, scale, colour):
    t = sub(text) if sub else text
    quads, _ = line_quads(f, t, mode)
    return render_quads(f, quads, scale, colour)[0]


def cmd_mockups(a):
    out = a.out
    written = []
    for hd in (False, True):
        _doc, fonts = load(out, hd)
        for f in fonts:
            specs = design(f)
            offline = {cp: compose_offline(f, cp, s) for cp, s in specs.items()}
            scale = 2  # about the 1080p size: the GUI is 800 x 600 units on screen
            colour = (255, 255, 255) if f.name == "Rekord18" else (255, 214, 120)
            label_w = 430
            rows = []
            for label, mode, sub in modes_for(f, specs, offline):
                imgs = [draw_block(f, s, mode, sub, scale, colour) for s in SAMPLES[f.name]]
                rows.append((label, imgs))
            width = label_w + max(sum(i.width for i in imgs) + 30 * len(imgs) for _, imgs in rows)
            row_h = max(i.height for _, imgs in rows for i in imgs) + 18
            sheet = Image.new("RGBA", (width, 40 + row_h * len(rows)), (0, 0, 0, 255))
            # a dark, game-like background (wood brown to blue)
            d = ImageDraw.Draw(sheet)
            for y in range(sheet.height):
                t = y / max(1, sheet.height - 1)
                d.line([(0, y), (width, y)], fill=(int(58 - 20 * t), int(44 - 4 * t), int(36 + 30 * t), 255))
            d.text((10, 8), "%s - %s atlas at 2x (about 1080p), using the game's text layout" % (f.name, "HD 2x" if hd else "original"),
                   fill=(240, 240, 240), font=ui_font(16, True))
            for r, (label, imgs) in enumerate(rows):
                y = 40 + r * row_h
                d.text((10, y + row_h // 2 - 10), label, fill=(225, 225, 225), font=ui_font(14))
                x = label_w
                for im in imgs:
                    sheet.alpha_composite(im, (x, y + (row_h - im.height) // 2))
                    x += im.width + 30
            p = os.path.join(out, "mockup_%s%s.png" % (f.name, "_hd" if hd else ""))
            sheet.convert("RGB").save(p)
            written.append(p)
            # close-up of the four letters: (a) and (b), 6x nearest
            z = 6
            tiles = []
            for cp in (0x150, 0x151, 0x170, 0x171):
                for name, mode in (("a", {"composites": specs}), ("b", {"composites": specs, "images": offline})):
                    q, _ = glyph_quads(f, cp, 0, mode)
                    im, _ = render_quads(f, q, 1 if not hd else 2)
                    tiles.append((chr(cp) + " " + name, im.resize((im.width * z // (1 if not hd else 2), im.height * z // (1 if not hd else 2)), Image.NEAREST)))
                for ref in ("Ö", "Ó") if cp in (0x150,) else ("ö", "ó") if cp == 0x151 else ("Ü", "Ú") if cp == 0x170 else ("ü", "ú"):
                    q, _ = glyph_quads(f, ord(ref), 0, {"remap": REMAP_FIX.get(f.name, {})})
                    im, _ = render_quads(f, q, 1 if not hd else 2)
                    tiles.append((ref + " original", im.resize((im.width * z // (1 if not hd else 2), im.height * z // (1 if not hd else 2)), Image.NEAREST)))
            tw = max(t[1].width for t in tiles) + 16
            th = max(t[1].height for t in tiles) + 30
            cols = 4
            cs = Image.new("RGBA", (cols * tw, ((len(tiles) + cols - 1) // cols) * th + 30), (30, 30, 36, 255))
            dd = ImageDraw.Draw(cs)
            dd.text((6, 6), "%s %s: (a) Draw-time composite | (b) Precomposed | Original accented letters, %dx" % (f.name, "HD" if hd else "SD", z),
                    fill=(235, 235, 235), font=ui_font(13))
            for i, (lab, im) in enumerate(tiles):
                x0, y0 = (i % cols) * tw + 8, (i // cols) * th + 30
                bg = checker(im.width, im.height, z, (150, 156, 166, 255), (172, 178, 188, 255))  # light: shows stray outline
                bg.alpha_composite(im)
                cs.paste(bg, (x0, y0 + 18))
                dd.text((x0, y0 + 2), lab, fill=(255, 215, 120), font=ui_font(13))
            p = os.path.join(out, "closeup_%s%s.png" % (f.name, "_hd" if hd else ""))
            cs.convert("RGB").save(p)
            written.append(p)
    for p in written:
        log("wrote", p)
    return 0


def cmd_context(a):
    """Solution a in a 1080p game frame (an earlier frame capture of the game, --frame): a subtitle as ASubtitle::Show
    places it (Rekord18, root width - 24, bottom centre, 16 units up) and a Rekord26 title, HD atlas, GUI scale 1080/600."""
    _doc, fonts = load(a.out, True)
    F = {f.name: f for f in fonts}
    bg = Image.open(a.frame).convert("RGBA")
    scale = bg.height / 600.0
    root_w = bg.width / scale
    for name, lines, colour, y_units in (
            ("Rekord26", ["ÚJ KÜLDETÉS: ŐRÜLT FŰZŐ"], (255, 214, 120), 40.0),
            ("Rekord18", ["Ottó, gyere gyorsan! Ő az, a tűzoltóautó!", "Győzd le őket, és tűzz el a sűrű erdőbe!"], (255, 255, 255), None)):
        f = F[name]
        specs = design(f)
        mode = {"remap": REMAP_FIX.get(name, {}), "composites": specs}
        adv = f.lh + f.lm
        top = y_units if y_units is not None else 600.0 - 16.0 - adv * len(lines)
        for i, text in enumerate(lines):
            quads, width = line_quads(f, text, mode)
            x = (root_w - width) / 2.0  # TextAlign_Center inside the box
            render_quads(f, quads, scale, colour, canvas=bg, origin=(x * scale, (top + i * adv) * scale))
    p = os.path.join(a.out, "context_1080p_hd.png")
    bg.convert("RGB").save(p)
    log("wrote", p)
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--data", default=DEF_DATA)
    p.add_argument("--out", default=DEF_OUT)
    sub = p.add_subparsers(dest="cmd", required=True)
    for c in ("extract", "coverage", "sheets", "specimen", "composites", "mockups"):
        sub.add_parser(c)
    sc = sub.add_parser("context")
    sc.add_argument("--frame", required=True, help="Path to a game frame image supplied by the user")
    a = p.parse_args()
    return {"extract": cmd_extract, "coverage": cmd_coverage, "sheets": cmd_sheets, "specimen": cmd_specimen,
            "composites": cmd_composites, "mockups": cmd_mockups, "context": cmd_context}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
