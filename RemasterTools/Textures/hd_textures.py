#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""hd_textures.py - HD texture pipeline for Barnyard (2006) / OpenBarnyard.     Needs: Python 3.10+ and Pillow, nothing else.

The game reads every texture from a "TTL" material library (a DDS file in memory per texture, named like
World\straw.tga). OpenBarnyard replaces such a texture with the loose file Data\Textures\<that name> when it exists.
This tool extracts the originals, runs them through an upscaler and writes packs in exactly that layout.

    set PYTHONIOENCODING=utf-8
    py hd_textures.py [--data <game>\Data] [--work <workdir>] <command> ...

    extract                      every unique texture name of every library -> <work>\src\*.png + <work>\manifest.json
    stats                        what is in the manifest (and which background pictures are left out, and why)
    upscale --exe <realesrgan-ncnn-vulkan.exe> --model <name>      4x -> <work>\up\*.png   (resumable)
    upscale --fake                                                 same chain with Lanczos -> <work>\up_fake\*.png
    override --method consistent|lanczos|model [--model M] <glob>  how 'upscale' makes these names, from now on
    override --clear <glob> | override                             remove / list the overrides (<work>\overrides.json)
    review [--sort lost|diff|added|patchy] [--sheet out.png]       which upscaled textures left their original most
    pack --out <dir> [--factor 2] [--up <dir>] [--clean]           DDS files named like the TTL names, full mip chains
    testpack --out <dir> [--clean]                                 the originals, same size and format, tinted magenta
    verify <dir> [--factor N]                                      re-reads a pack against the manifest

Selection switches of upscale / review / pack / testpack (the default is the pictures used in 3D only, category "world"):
    --only <glob>        names matching the pattern (repeatable, case-insensitive, / or \), e.g. --only "world/*"
    --include-hud        also hud\, fonts\, dialogs\ and every texture cut up by GUI texture sections
                         (their UVs depend on the pixel size, see the manifest field "gui_sections")
    --include-bkg        also the bkg_* tiles of the 2D background pictures (as whole pictures, see below)
    --no-localized-bkg   ... but not the copies for other languages (<picture>D / E / F / I / N); English never loads them
    --include-lut        also the 32 / 64 pixel light ramps of the skin shader (Char\Lighting\*): look-up data
    --include-conflicts  also names that are different pictures in different libraries
    --sample N / --limit N   a spread out sample / the first N of the selection

How the upscale step treats a texture (see cmd_upscale):
    - 16 pixels are added on every side first and cut off after: the picture itself repeated where it tiles
      (manifest "wrap", per axis), its edge pixels where it does not. That keeps tiling textures seamless.
    - the colour of the visible pixels is bled into the fully transparent ones, so no dark halo appears
    - the exe only gets opaque RGB pictures, flat in one folder, a batch at a time (it is started once per batch),
      and every picture as ONE tile (-t): left to itself the exe cuts pictures above 200 pixels into tiles and the
      model then draws different detail in every tile (visible patches). Files made that way are redone.
    - alpha is upscaled here (bicubic), or with --alpha model in a second picture through the same model
    - the result is compared with the input (size, rough content) before it is accepted; failures are retried
      by simply running the same command again
    - a name with an override is made the way the override says (see cmd_override); the PNG text of every
      upscaled file says how it was made, and a later run redoes exactly the files that would be made differently

Background pictures (bkg_<name>_1..6.tga, 3 x 2 tiles that share one pixel column / row): the six tiles are put
together, the whole picture goes through the upscaler and is cut into the same tiles again, so no seam appears
between tiles. Pictures whose tiles do not form that grid are left out and listed. pack writes these tiles as DXT1
(a sixth of the bytes; the originals are uncompressed R8G8B8) unless --bkg-format R8G8B8, --bkg-rgb <glob> or
--bkg-rgb-below <PSNR in dB> say otherwise; --bkg-rgb-below 1 only reports what DXT1 costs.

<out> of pack / testpack is meant to be copied to <game>\Data\Textures. Nothing is ever written to --data.
Heavy work runs at BELOW_NORMAL priority. Exit code: 0 fine, 1 something failed, 2 wrong usage / missing input.
"""
import argparse
import ctypes
import fnmatch
import hashlib
import io
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageMath, ImageStat
from PIL.PngImagePlugin import PngInfo

VERSION = 2
DEF_DATA = os.path.join("Game", "Data")
DEF_WORK = "texture-work"
BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
GUI_FOLDERS = ("hud", "fonts", "dialogs")
SCALE = 4  # what the upscale step produces
METHODS = ("model", "consistent", "lanczos")  # see cmd_override

Image.MAX_IMAGE_PIXELS = None


def log(*a):
    print(*a, flush=True)


def low_priority():
    if os.name != "nt":
        return
    try:
        k = ctypes.windll.kernel32
        k.GetCurrentProcess.restype = ctypes.c_void_p
        k.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        k.SetPriorityClass(k.GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)
    except Exception as e:  # not fatal
        log("note: could not lower the priority:", e)


def write_atomic(path, data):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def save_png(im, path, info=None, level=4):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    tmp = path + ".tmp"
    meta = None
    if info is not None:
        meta = PngInfo()
        meta.add_text("hd_textures", json.dumps(info, sort_keys=True))
    im.save(tmp, "PNG", compress_level=level, pnginfo=meta)
    os.replace(tmp, path)


def fmt_bytes(n):
    return "%.1f MB" % (n / 1048576.0) if n >= 1048576 else "%.1f KB" % (n / 1024.0)


# ---------------------------------------------------------------------------------------------------------------------
# TRB / TTL (TOSHI 2.0, little endian):  'TSFL' size 'TRBF'  then hunks HDRX, SECT or SECC (BTEC compressed), RELC, SYMB
# ---------------------------------------------------------------------------------------------------------------------
U = struct.unpack_from


def btec_decompress(d, pos):
    """One BTEC stream at d[pos:]; returns (data, position after it)."""
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
        if b & 0x80:  # raw bytes
            out += d[pos:pos + n]
            pos += n
        else:  # copy from what is already unpacked (may overlap)
            off = d[pos]
            pos += 1
            if off & 0x80:
                off = ((off & 0x7F) << 8) | d[pos]
                pos += 1
            off += 1
            s = len(out) - off
            if s < 0:
                raise ValueError("BTEC offset before the start")
            if off >= n:
                out += out[s:s + n]
            else:
                out += (bytes(out[s:s + off]) * (n // off + 1))[:n]
    if len(out) != size:
        raise ValueError("BTEC size %d, expected %d" % (len(out), size))
    if xor is not None:
        out = bytearray(v ^ xor for v in out)
    return bytes(out), end


class TRB:
    """Sections, relocations and symbols of a TRB file. Sections are only unpacked when asked for."""

    def __init__(self, d):
        if len(d) < 12 or d[:4] != b"TSFL" or d[8:12] != b"TRBF":
            raise ValueError("not a little endian TRB")
        self.d = d
        hunks = {}
        p = 12
        while p + 8 <= len(d):
            size = U("<I", d, p + 4)[0]
            hunks[d[p:p + 4]] = p + 8
            p += 8 + ((size + 3) & ~3)
        if b"HDRX" not in hunks or b"SYMB" not in hunks:
            raise ValueError("no HDRX / SYMB hunk")
        self.hunks = hunks
        yo = hunks[b"SYMB"]
        n = U("<I", d, yo)[0]
        names = yo + 4 + n * 12
        self.symbols = []
        for i in range(n):
            sec, noff, _pad, _hash, doff = U("<HHHHI", d, yo + 4 + i * 12)
            self.symbols.append((d[names + noff:d.index(b"\0", names + noff)].decode("latin1"), sec, doff))
        self._secs = None
        self._rel = None

    def _load(self):
        if self._secs is not None:
            return
        d, hunks = self.d, self.hunks
        ho = hunks[b"HDRX"]
        sizes = [U("<I", d, ho + 12 + i * 16)[0] for i in range(U("<I", d, ho + 4)[0])]
        secs = []
        if b"SECT" in hunks:
            cur = hunks[b"SECT"]
            for s in sizes:
                secs.append(d[cur:cur + s])
                cur += s
        elif b"SECC" in hunks:
            cur = hunks[b"SECC"]
            for s in sizes:
                data, cur = btec_decompress(d, cur)
                if len(data) != s:
                    raise ValueError("section size mismatch")
                secs.append(data)
        else:
            raise ValueError("no SECT / SECC hunk")
        rel = {}
        if b"RELC" in hunks:
            ro = hunks[b"RELC"]
            for i in range(U("<I", d, ro)[0]):
                s1, s2, off = U("<HHI", d, ro + 4 + i * 8)
                rel[(s1, off)] = s2
        self._secs, self._rel = secs, rel

    def sec(self, i):
        self._load()
        return self._secs[i]

    def ptr(self, sec, off):
        """(target section, offset) of the pointer stored at sec:off; None when it is not relocated (null)."""
        self._load()
        target = self._rel.get((sec, off))
        if target is None:
            return None
        return target, U("<I", self._secs[sec], off)[0]

    def cstr(self, sec, off):
        b = self.sec(sec)
        return b[off:b.index(b"\0", off)].decode("latin1")


def read_ttl(trb, sec, off):
    """Textures of a TTL symbol: [(name, dds bytes)]. Raises ValueError for another layout.
    struct TTL { int iNumTextures; TexInfo* pTextureInfos; const char* szPackName; }
    struct TexInfo { int eFormat; const char* szFileName; uint uiDataSize; void* pData; }"""
    count = U("<i", trb.sec(sec), off)[0]
    if not 0 <= count <= 20000:
        raise ValueError("texture count %d" % count)
    if count == 0:
        return []
    infos = trb.ptr(sec, off + 4)
    if infos is None:
        raise ValueError("no texture info pointer")
    isec, ioff = infos
    out = []
    for i in range(count):
        o = ioff + i * 16
        if o + 16 > len(trb.sec(isec)):
            raise ValueError("texture info outside the section")
        efmt, _n, size, _d = U("<IIII", trb.sec(isec), o)
        pname, pdata = trb.ptr(isec, o + 4), trb.ptr(isec, o + 12)
        if pname is None or pdata is None:
            raise ValueError("texture %d: unrelocated name / data pointer (not the DDS based TTL layout)" % i)
        data = trb.sec(pdata[0])[pdata[1]:pdata[1] + size]
        if efmt != 1 or len(data) != size or data[:4] != b"DDS " or size < 128 or U("<I", data, 4)[0] != 124:
            raise ValueError("texture %d: eFormat %d, not a DDS file" % (i, efmt))
        out.append((trb.cstr(*pname), data))
    return out


def read_texture_sections(trb, sec, off):
    """Texture file names used by a 'texturesections' block (T2GUITextureSectionManager::TRBHeader):
    {name.lower(): number of sections}. A section is 60 bytes: name*, texture file*, x1, y1, x2, y2, material*, 2 x vec4."""
    count = U("<I", trb.sec(sec), off + 4)[0]
    first = trb.ptr(sec, off)
    out = {}
    if first is None or count > 100000:
        return out
    ssec, soff = first
    for i in range(count):
        p = trb.ptr(ssec, soff + i * 60 + 4)
        if p is not None:
            n = trb.cstr(*p).replace("/", "\\").lower()
            out[n] = out.get(n, 0) + 1
    return out


# ---------------------------------------------------------------------------------------------------------------------
# DDS reading
# ---------------------------------------------------------------------------------------------------------------------
def dds_info(dds):
    """Format, size and mip count of a DDS file in memory."""
    _size, flags, h, w, _pitch, _depth, mips = U("<7I", dds, 4)
    pf_flags, fourcc, bits, rm, gm, bm, am = U("<I4sIIIII", dds, 80)
    if pf_flags & 0x4:
        fmt = fourcc.decode("latin1")
        if fmt not in ("DXT1", "DXT3", "DXT5"):
            raise ValueError("FourCC %r" % fourcc)
    else:
        key = (bits, rm, gm, bm, am if pf_flags & 0x1 else 0)
        fmt = {
            (24, 0xFF0000, 0xFF00, 0xFF, 0): "R8G8B8",
            (32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000): "A8R8G8B8",
            (32, 0xFF0000, 0xFF00, 0xFF, 0): "X8R8G8B8",
            (16, 0x7C00, 0x3E0, 0x1F, 0x8000): "A1R5G5B5",
            (16, 0x7C00, 0x3E0, 0x1F, 0): "X1R5G5B5",
            (16, 0xF800, 0x7E0, 0x1F, 0): "R5G6B5",
            (16, 0xF00, 0xF0, 0xF, 0xF000): "A4R4G4B4",
        }.get(key)
        if fmt is None:
            raise ValueError("pixel format bits=%d masks=%x,%x,%x,%x flags=%x" % (bits, rm, gm, bm, am, pf_flags))
    return {"format": fmt, "width": w, "height": h, "mips": mips if (flags & 0x20000) and mips else 1}


RAW_MODES = {  # format: (image mode, raw mode, bytes per pixel)
    "R8G8B8": ("RGB", "BGR", 3), "A8R8G8B8": ("RGBA", "BGRA", 4), "X8R8G8B8": ("RGB", "BGRX", 4),
    "A1R5G5B5": ("RGBA", "BGRA;15", 2), "X1R5G5B5": ("RGB", "BGR;15", 2), "R5G6B5": ("RGB", "BGR;16", 2),
    "A4R4G4B4": ("RGBA", "RGBA;4B", 2),
}
HAS_ALPHA = {"DXT1": True, "DXT3": True, "DXT5": True, "A8R8G8B8": True, "A1R5G5B5": True, "A4R4G4B4": True}
BIT_RANK = {"A8R8G8B8": 3, "X8R8G8B8": 3, "R8G8B8": 3, "DXT5": 2, "DXT3": 2, "DXT1": 2}


def decode_top(dds, info):
    """Top mip level as an RGBA (formats with alpha) or RGB image."""
    fmt, w, h = info["format"], info["width"], info["height"]
    if fmt in RAW_MODES:
        mode, raw, bpp = RAW_MODES[fmt]
        data = dds[128:128 + w * h * bpp]
        if len(data) != w * h * bpp:
            raise ValueError("DDS data too short")
        return Image.frombuffer(mode, (w, h), data, "raw", raw, 0, 1).copy()
    im = Image.open(io.BytesIO(dds))
    im.load()
    if im.size != (w, h):
        raise ValueError("decoder size mismatch")
    return im.convert("RGBA")


def out_format(fmt, alpha_used):
    """What a pack writes for an original of this format: the same format class (see AModelLoader::CreateMaterial,
    which blends A8R8G8B8 / A4R4G4B4 / DXT5 textures and draws everything else opaque)."""
    if fmt in ("DXT3", "DXT5", "A8R8G8B8", "R8G8B8"):
        return fmt
    if fmt == "DXT1":
        return "DXT3" if alpha_used else "DXT1"  # DXT3: still the opaque class, keeps the cut-out alpha
    if fmt == "A4R4G4B4":
        return "A8R8G8B8"
    if fmt in ("A1R5G5B5", "X1R5G5B5", "R5G6B5", "X8R8G8B8"):
        return "DXT3" if alpha_used else "R8G8B8"
    raise ValueError(fmt)


# ---------------------------------------------------------------------------------------------------------------------
# Image helpers (colour and alpha are always resampled separately: Pillow premultiplies RGBA images when it resizes
# them, which blackens the colour of transparent pixels)
# ---------------------------------------------------------------------------------------------------------------------
def split_rgba(im):
    """(RGB image, alpha L image or None)."""
    if im.mode == "RGBA":
        return im.convert("RGB"), im.getchannel("A")
    return im.convert("RGB"), None


def merge_rgba(rgb, alpha):
    if alpha is None:
        return rgb
    return Image.merge("RGBA", rgb.split() + (alpha,))


def bleed(rgb, alpha):
    """Colour of the visible pixels pulled into the fully transparent ones (pull-push), so that neither the upscaler
    nor texture filtering nor the block compressor mixes in whatever colour was stored there (usually black)."""
    lo, hi = alpha.getextrema()
    if lo > 0 or hi == 0:
        return rgb
    mask = alpha.point([0] + [255] * 255)
    cur = merge_rgba(rgb, mask)
    pyramid = [cur]
    while cur.size != (1, 1):
        size = (max(1, cur.width // 2), max(1, cur.height // 2))
        cur = cur.resize(size, Image.BOX)  # RGBA: averaged with the coverage as weight
        cur.putalpha(cur.getchannel("A").point([0] + [255] * 255))
        pyramid.append(cur)
    fill = pyramid[-1].convert("RGB")
    for level in reversed(pyramid[:-1]):
        fill = fill.resize(level.size, Image.BILINEAR)
        fill.paste(level.convert("RGB"), (0, 0), level.getchannel("A"))
    return fill


def pad_image(im, pad, wrap):
    """Adds pad pixels on every side: the image repeated (wrap) or its edge pixels (clamp), per axis."""
    w, h = im.size
    out = Image.new(im.mode, (w + 2 * pad, h))
    if wrap[0]:
        for k in range(-((pad + w - 1) // w), (pad + w - 1) // w + 1):
            out.paste(im, (pad + k * w, 0))
    else:
        out.paste(im, (pad, 0))
        out.paste(im.crop((0, 0, 1, h)).resize((pad, h), Image.NEAREST), (0, 0))
        out.paste(im.crop((w - 1, 0, w, h)).resize((pad, h), Image.NEAREST), (pad + w, 0))
    im, w = out, w + 2 * pad
    out = Image.new(im.mode, (w, h + 2 * pad))
    if wrap[1]:
        for k in range(-((pad + h - 1) // h), (pad + h - 1) // h + 1):
            out.paste(im, (0, pad + k * h))
    else:
        out.paste(im, (0, pad))
        out.paste(im.crop((0, 0, w, 1)).resize((w, pad), Image.NEAREST), (0, 0))
        out.paste(im.crop((0, h - 1, w, h)).resize((w, pad), Image.NEAREST), (0, pad + h))
    return out


def mean_abs_diff(a, b):
    st = ImageStat.Stat(ImageChops.difference(a, b))
    return sum(st.mean) / len(st.mean)


def thumbnail(rgb, alpha, n=16):
    """(n x n colour, n x n alpha or None); with alpha the colour is the average of what is visible."""
    if alpha is None:
        return rgb.resize((n, n), Image.BOX), None
    im = merge_rgba(rgb, alpha).resize((n, n), Image.BOX)  # RGBA: Pillow weights the colour with alpha here
    return im.convert("RGB"), im.getchannel("A")


def seam_stats(im):
    """How well the image continues across its left/right and top/bottom edges when repeated:
    [seam x, inside x, seam y, inside y], each the mean absolute difference of neighbouring pixel columns / rows
    (colour of invisible pixels ignored)."""
    rgb, alpha = split_rgba(im)
    if alpha is not None:
        rgb = Image.composite(rgb, Image.new("RGB", im.size), alpha.point([0] + [255] * 255))
        test = merge_rgba(rgb, alpha)
    else:
        test = rgb
    w, h = im.size
    out = []
    for dx, dy in ((1, 0), (0, 1)):
        d = ImageChops.difference(test, ImageChops.offset(test, dx, dy))
        seam = d.crop((0, 0, 1, h) if dx else (0, 0, w, 1))
        inner = d.crop((1, 0, w, h) if dx else (0, 1, w, h))
        s = sum(ImageStat.Stat(seam).mean) / len(test.getbands())
        i = sum(ImageStat.Stat(inner).mean) / len(test.getbands()) if inner.size[0] and inner.size[1] else s
        out += [round(s, 2), round(i, 2)]
    return out


def tiles(seam, inner):
    """The repeat heuristic: the jump across the edge is not much bigger than the jumps inside the picture."""
    return seam <= 2.5 * inner + 6.0


def back_project(out, src):
    """out + enlarged (src - out reduced to the size of src). Puts back what the model changed at the original
    resolution - grain it smoothed away, shapes it moved or invented - and keeps the detail it added below that."""
    small = out.resize(src.size, Image.BOX)
    chans = []
    for o, s, d in zip(out.split(), src.split(), small.split()):
        diff = _EV(lambda a: a["float"](a["s"]) - a["float"](a["d"]), s=s, d=d).resize(out.size, Image.LANCZOS)
        chans.append(_EV(lambda a: a["float"](a["o"]) + a["d"] + 0.5, o=o, d=diff).convert("L"))  # F -> L clips
    return Image.merge("RGB", chans)


def lanczos_up(rgb, sharpen=True):
    """The plain enlargement: Lanczos, by default with a mild unsharp mask (keeps every glyph and icon as it is)."""
    out = rgb.resize((rgb.width * SCALE, rgb.height * SCALE), Image.LANCZOS)
    return out.filter(ImageFilter.UnsharpMask(radius=3, percent=60, threshold=0)) if sharpen else out


# ---------------------------------------------------------------------------------------------------------------------
# 2D background pictures. AGUIMatlibPicture draws <picture>_1.tga .. _6.tga as 3 columns x 2 rows (the "split rules"
# in AGUIPicture.cpp): neighbouring tiles share one pixel column / row, so the picture is 255 + 255 + 128 = 638 wide
# and 255 + 256 = 511 high. Only its rows 0..478 are picture (the game shows 477 of them, Render scales by 638 x 477);
# the bottom tiles are plain white from their row 224 on in every clean picture.
# ---------------------------------------------------------------------------------------------------------------------
PIC_TILES = [(0, 0, 256, 256), (255, 0, 256, 256), (510, 0, 128, 256),
             (0, 255, 256, 256), (255, 255, 256, 256), (510, 255, 128, 256)]  # x, y, width, height of tile 1..6
PIC_SIZE = (638, 511)
PIC_SHOWN_ROWS = 479


def find_pictures(man, open_src):
    """{picture: {"tiles": [names], "clean": bool, "why": text}} for the bkg_* names of the manifest. clean = six tiles
    of the standard sizes that agree in the pixels they share; only such a picture can be put together and cut again."""
    groups = {}
    for name, t in man["textures"].items():
        if t["category"] == "bkg":
            m = re.match(r"^(.*)_(\d+)\.tga$", name, re.I)
            groups.setdefault(m.group(1) if m else name, {})[int(m.group(2)) if m else 0] = name
    out = {}
    for pic in sorted(groups, key=str.lower):
        g = groups[pic]
        e = out[pic] = {"tiles": [g[i] for i in sorted(g)], "clean": False, "why": ""}
        if sorted(g) != [1, 2, 3, 4, 5, 6]:
            e["why"] = "its tiles are numbered %s, not 1..6" % sorted(g)
            continue
        sizes = [(man["textures"][n]["width"], man["textures"][n]["height"]) for n in e["tiles"]]
        if sizes != [(w, h) for _x, _y, w, h in PIC_TILES]:
            e["why"] = "tile sizes %s instead of 256x256 256x256 128x256 twice" % " ".join("%dx%d" % s for s in sizes)
            continue
        ims = [open_src(n).convert("RGB") for n in e["tiles"]]
        worst = 0.0
        for i, j in ((0, 1), (1, 2), (3, 4), (4, 5)):  # the shared pixel columns
            worst = max(worst, mean_abs_diff(ims[i].crop((255, 0, 256, 256)), ims[j].crop((0, 0, 1, 256))))
        for i, j in ((0, 3), (1, 4), (2, 5)):  # the shared pixel rows
            w = ims[i].width
            worst = max(worst, mean_abs_diff(ims[i].crop((0, 255, w, 256)), ims[j].crop((0, 0, w, 1))))
        if worst > 2.0:
            e["why"] = "the tiles do not agree in the pixel column / row they share (mean difference %.1f)" % worst
            continue
        e["clean"] = True
    return out


def assemble_picture(tile_images):
    """The six tiles put together (638 x 511). The rows below the picture repeat its last row, so that the upscaler
    does not see a hard white edge there (it would draw a halo into the last rows the game shows)."""
    pic = Image.new("RGB", PIC_SIZE)
    for im, (x, y, _w, _h) in zip(tile_images, PIC_TILES):
        pic.paste(im.convert("RGB"), (x, y))
    last = pic.crop((0, PIC_SHOWN_ROWS - 1, PIC_SIZE[0], PIC_SHOWN_ROWS))
    pic.paste(last.resize((PIC_SIZE[0], PIC_SIZE[1] - PIC_SHOWN_ROWS), Image.NEAREST), (0, PIC_SHOWN_ROWS))
    return pic


def cut_picture(big, tile_images):
    """The enlarged picture cut back into the six tiles (shared columns / rows included, so the tiles still agree
    there). Below the picture the bottom tiles get their own rows back (white), enlarged plainly: the border between
    picture and white stays on a multiple of 4 rows, so no DXT block and no mip level mixes them more than before."""
    s = big.width // PIC_SIZE[0]
    out = []
    for im, (x, y, w, h) in zip(tile_images, PIC_TILES):
        tile = big.crop((x * s, y * s, (x + w) * s, (y + h) * s))
        keep = PIC_SHOWN_ROWS - y
        if keep < h:
            tile.paste(im.convert("RGB").crop((0, keep, w, h)).resize((w * s, (h - keep) * s), Image.BICUBIC), (0, keep * s))
        out.append(tile)
    return out


# ---------------------------------------------------------------------------------------------------------------------
# DXT1 / DXT3 / DXT5 encoder, vectorised with Pillow's ImageMath (principal axis range fit + one least squares pass,
# and a second candidate for blocks without contrast, see the end of _bc1_color_streams).
# Pillow's own DDS writer is not used: its DXT3 alpha is wrong for a quarter of the pixels (12.3.0) and its colour
# blocks are 2-4 dB worse.
# ---------------------------------------------------------------------------------------------------------------------
_EV = ImageMath.lambda_eval
_SHL = {n: bytes(((v << n) & 255) for v in range(256)) for n in range(1, 8)}
_TAB = {
    "and3shl6": bytes(((v & 3) << 6) & 255 for v in range(256)),
    "shr2": bytes((v >> 2) & 255 for v in range(256)),
    "and1shl7": bytes(((v & 1) << 7) & 255 for v in range(256)),
    "shr1": bytes((v >> 1) & 255 for v in range(256)),
}
_LUT_BC1 = [1, 3, 2, 0] + [0] * 252
_LUT_BC3 = [1, 7, 6, 5, 4, 3, 2, 0] + [0] * 248
_LUT_A4 = [(v * 15 + 127) // 255 for v in range(256)]


def _flat_tables(bits):
    """For every 8 bit value the end points (c0 channel, c1 channel) whose 2/3 point (2 * c0 + c1) / 3 comes closest
    to it: what a block of one colour needs. Reaches about +-1.4 levels with 5 bits and +-0.7 with 6, where rounding
    the colour itself is off by up to 4 and 2."""
    top = (1 << bits) - 1
    exp = [(q << (8 - bits)) | (q >> (2 * bits - 8)) for q in range(top + 1)]
    hi, lo = [], []
    for v in range(256):
        best = None
        for l in range(top + 1):
            for h in range(l, min(top, l + 2) + 1):
                err = abs((2 * exp[h] + exp[l]) / 3.0 - v) + 0.01 * (h - l)
                if best is None or err < best[0]:
                    best = (err, h, l)
        hi.append(best[1])
        lo.append(best[2])
    return hi, lo


_FLAT5, _FLAT6 = _flat_tables(5), _flat_tables(6)


def _or(*streams):
    """Byte-wise OR of equally long byte strings."""
    acc = 0
    for s in streams:
        acc |= int.from_bytes(s, "little")
    return acc.to_bytes(len(streams[0]), "little")


def _sub16(im):
    """The 16 images holding pixel (i, j) of every 4x4 block, in block pixel order."""
    w4, h4 = im.width // 4, im.height // 4
    return [im.transform((w4, h4), Image.AFFINE, (4, 0, i - 1.5, 0, 4, j - 1.5), Image.NEAREST)
            for j in range(4) for i in range(4)]


def _up(im, size):
    return im.resize(size, Image.NEAREST)


def pad4(im):
    """Edge-replicates the image up to multiples of 4 (what the blocks of a small mip level hold)."""
    w, h = im.size
    w4, h4 = (w + 3) & ~3, (h + 3) & ~3
    if (w4, h4) == (w, h):
        return im
    out = Image.new(im.mode, (w4, h4))
    out.paste(im, (0, 0))
    if w4 > w:
        out.paste(im.crop((w - 1, 0, w, h)).resize((w4 - w, h), Image.NEAREST), (w, 0))
    if h4 > h:
        out.paste(out.crop((0, h - 1, w4, h)).resize((w4, h4 - h), Image.NEAREST), (0, h))
    return out


def _bc1_color_streams(rgb, refine):
    """8 byte streams (a byte per block each) of the colour block of every 4x4 block: c0, c1 (565), 4 index bytes.
    c0 >= c1 always; with c0 == c1 only the indices 0 / 1 are used, so the block is valid in DXT1 too."""
    size = rgb.size
    r, g, b = [c.convert("F") for c in rgb.split()]
    mr, mg, mb = r.reduce(4), g.reduce(4), b.reduce(4)

    def cov(x, y, mx, my):
        return _EV(lambda a: a["p"] - a["mx"] * a["my"], p=_EV(lambda a: a["x"] * a["y"], x=x, y=y).reduce(4), mx=mx, my=my)

    crr, cgg, cbb = cov(r, r, mr, mr), cov(g, g, mg, mg), cov(b, b, mb, mb)
    crg, crb, cgb = cov(r, g, mr, mg), cov(r, b, mr, mb), cov(g, b, mg, mb)

    # Principal axis of the block's colours by power iteration
    vr = _EV(lambda a: a["v"] + a["d"] * 0.001, v=crg, d=crr)
    vg = _EV(lambda a: a["v"] + 1e-6, v=cgg)
    vb = _EV(lambda a: a["v"] + a["d"] * 0.001, v=cgb, d=cbb)
    for _ in range(4):
        nr = _EV(lambda a: a["crr"] * a["vr"] + a["crg"] * a["vg"] + a["crb"] * a["vb"], crr=crr, crg=crg, crb=crb, vr=vr, vg=vg, vb=vb)
        ng = _EV(lambda a: a["crg"] * a["vr"] + a["cgg"] * a["vg"] + a["cgb"] * a["vb"], crg=crg, cgg=cgg, cgb=cgb, vr=vr, vg=vg, vb=vb)
        nb = _EV(lambda a: a["crb"] * a["vr"] + a["cgb"] * a["vg"] + a["cbb"] * a["vb"], crb=crb, cgb=cgb, cbb=cbb, vr=vr, vg=vg, vb=vb)
        nrm = _EV(lambda a: a["max"](a["max"](abs(a["r"]), abs(a["g"])), a["max"](abs(a["b"]), 1e-12)), r=nr, g=ng, b=nb)
        vr = _EV(lambda a: a["v"] / a["n"], v=nr, n=nrm)
        vg = _EV(lambda a: a["v"] / a["n"], v=ng, n=nrm)
        vb = _EV(lambda a: a["v"] / a["n"], v=nb, n=nrm)

    # Every pixel projected on the axis; the extremes of a block give its first end points
    d = _EV(lambda a: (a["r"] - a["mr"]) * a["vr"] + (a["g"] - a["mg"]) * a["vg"] + (a["b"] - a["mb"]) * a["vb"],
            r=r, g=g, b=b, mr=_up(mr, size), mg=_up(mg, size), mb=_up(mb, size), vr=_up(vr, size), vg=_up(vg, size), vb=_up(vb, size))
    subs = _sub16(d)
    dmin = dmax = subs[0]
    for s in subs[1:]:
        dmin = _EV(lambda a: a["min"](a["x"], a["y"]), x=dmin, y=s)
        dmax = _EV(lambda a: a["max"](a["x"], a["y"]), x=dmax, y=s)
    vv = _EV(lambda a: a["max"](a["r"] * a["r"] + a["g"] * a["g"] + a["b"] * a["b"], 1e-12), r=vr, g=vg, b=vb)

    def endpoint(m, v, t):
        return _EV(lambda a: a["min"](a["max"](a["m"] + a["v"] * a["t"] / a["vv"], 0), 255), m=m, v=v, t=t, vv=vv)

    def quant(e):  # float end point -> 565 value (I image)
        return _EV(lambda a: (a["int"](a["r"] * (31 / 255) + 0.5) << 11) | (a["int"](a["g"] * (63 / 255) + 0.5) << 5) | a["int"](a["b"] * (31 / 255) + 0.5),
                   r=e[0], g=e[1], b=e[2])

    def expand(c):  # the colour a decoder makes of a 565 value (F images)
        r5 = _EV(lambda a: a["c"] >> 11, c=c)
        g6 = _EV(lambda a: (a["c"] >> 5) & 63, c=c)
        b5 = _EV(lambda a: a["c"] & 31, c=c)
        return [_EV(lambda a: a["float"]((a["x"] << 3) | (a["x"] >> 2)), x=r5),
                _EV(lambda a: a["float"]((a["x"] << 2) | (a["x"] >> 4)), x=g6),
                _EV(lambda a: a["float"]((a["x"] << 3) | (a["x"] >> 2)), x=b5)]

    def order(ca, cb):
        return (_EV(lambda a: a["max"](a["x"], a["y"]), x=ca, y=cb), _EV(lambda a: a["min"](a["x"], a["y"]), x=ca, y=cb))

    def levels(c0, c1):  # 0..3 per pixel: its place on the line from c1 (0) to c0 (3)
        e0, e1 = expand(c0), expand(c1)
        dr, dg, db = [_EV(lambda a: a["x"] - a["y"], x=x, y=y) for x, y in zip(e0, e1)]
        inv = _EV(lambda a: 3.0 / a["max"](a["r"] * a["r"] + a["g"] * a["g"] + a["b"] * a["b"], 1e-6), r=dr, g=dg, b=db)
        return _EV(lambda a: a["int"](a["min"](a["max"](((a["r"] - a["r1"]) * a["dr"] + (a["g"] - a["g1"]) * a["dg"] + (a["b"] - a["b1"]) * a["db"]) * a["inv"] + 0.5, 0), 3)),
                   r=r, g=g, b=b, r1=_up(e1[0], size), g1=_up(e1[1], size), b1=_up(e1[2], size),
                   dr=_up(dr, size), dg=_up(dg, size), db=_up(db, size), inv=_up(inv, size))

    c0, c1 = order(quant([endpoint(mr, vr, dmax), endpoint(mg, vg, dmax), endpoint(mb, vb, dmax)]),
                   quant([endpoint(mr, vr, dmin), endpoint(mg, vg, dmin), endpoint(mb, vb, dmin)]))
    lv = levels(c0, c1)

    for _ in range(refine):
        # Least squares end points for these levels: minimise sum |w*e0 + (1-w)*e1 - p|^2 with w = level / 3
        w = _EV(lambda a: a["float"](a["l"]) / 3.0, l=lv)
        aa = _EV(lambda a: a["w"] * a["w"], w=w).reduce(4)
        ab = _EV(lambda a: a["w"] * (1 - a["w"]), w=w).reduce(4)
        bb = _EV(lambda a: (1 - a["w"]) * (1 - a["w"]), w=w).reduce(4)
        det = _EV(lambda a: a["aa"] * a["bb"] - a["ab"] * a["ab"], aa=aa, ab=ab, bb=bb)
        ok = _EV(lambda a: a["min"](a["max"]((a["d"] - 1e-5) * 1e9, 0), 1), d=det)  # 1 where the block uses two levels or more
        sdet = _EV(lambda a: a["max"](a["d"], 1e-9), d=det)
        new0, new1 = [], []
        for ch, old0, old1 in zip((r, g, b), expand(c0), expand(c1)):
            pa = _EV(lambda a: a["w"] * a["p"], w=w, p=ch).reduce(4)
            pb = _EV(lambda a: (1 - a["w"]) * a["p"], w=w, p=ch).reduce(4)
            n0 = _EV(lambda a: (a["pa"] * a["bb"] - a["pb"] * a["ab"]) / a["d"], pa=pa, pb=pb, bb=bb, ab=ab, d=sdet)
            n1 = _EV(lambda a: (a["pb"] * a["aa"] - a["pa"] * a["ab"]) / a["d"], pa=pa, pb=pb, aa=aa, ab=ab, d=sdet)
            new0.append(_EV(lambda a: a["min"](a["max"](a["n"] * a["ok"] + a["o"] * (1 - a["ok"]), 0), 255), n=n0, o=old0, ok=ok))
            new1.append(_EV(lambda a: a["min"](a["max"](a["n"] * a["ok"] + a["o"] * (1 - a["ok"]), 0), 255), n=n1, o=old1, ok=ok))
        c0, c1 = order(quant(new0), quant(new1))
        lv = levels(c0, c1)

    # A second candidate for blocks with next to no contrast. Their end points round to the same or to neighbouring
    # 565 colours, which leaves the whole block up to 4 levels off: coloured bands and a mosaic of blocks in smooth
    # gradients (painted backgrounds, skies, flat coloured skins). End points picked so that the 2/3 point of their
    # line is the block's mean come within about a level of it; the block's pixels still choose among the four
    # colours of that line. Every block takes the candidate with the smaller squared error.
    def squared_error(ca, cb, lvx):
        w = _EV(lambda a: a["float"](a["l"]) / 3.0, l=lvx)
        tot = None
        for ch, a0, a1 in zip((r, g, b), expand(ca), expand(cb)):
            d = _EV(lambda a: (lambda x: x * x)(a["a1"] + (a["a0"] - a["a1"]) * a["w"] - a["p"]), a0=_up(a0, size), a1=_up(a1, size), w=w, p=ch)
            tot = d if tot is None else _EV(lambda a: a["x"] + a["y"], x=tot, y=d)
        return tot.reduce(4)

    def from_mean(m, table):
        return _EV(lambda a: a["int"](a["m"] + 0.5), m=m).convert("L").point(table).convert("I")

    f0 = _EV(lambda a: (a["r"] << 11) | (a["g"] << 5) | a["b"], r=from_mean(mr, _FLAT5[0]), g=from_mean(mg, _FLAT6[0]), b=from_mean(mb, _FLAT5[0]))
    f1 = _EV(lambda a: (a["r"] << 11) | (a["g"] << 5) | a["b"], r=from_mean(mr, _FLAT5[1]), g=from_mean(mg, _FLAT6[1]), b=from_mean(mb, _FLAT5[1]))
    flv = levels(f0, f1)
    better = _EV(lambda a: a["int"](a["f"] < a["x"]), f=squared_error(f0, f1, flv), x=squared_error(c0, c1, lv))  # I image, 1 / 0 per block
    c0 = _EV(lambda a: a["f"] * a["m"] + a["x"] * (1 - a["m"]), f=f0, x=c0, m=better)
    c1 = _EV(lambda a: a["f"] * a["m"] + a["x"] * (1 - a["m"]), f=f1, x=c1, m=better)
    lv = _EV(lambda a: a["f"] * a["m"] + a["x"] * (1 - a["m"]), f=flv, x=lv, m=_up(better, size))

    s = [x.tobytes() for x in _sub16(lv.convert("L").point(_LUT_BC1))]
    rows = [_or(s[4 * k], s[4 * k + 1].translate(_SHL[2]), s[4 * k + 2].translate(_SHL[4]), s[4 * k + 3].translate(_SHL[6])) for k in range(4)]
    b0, b1 = c0.tobytes(), c1.tobytes()  # I images: 4 little endian bytes per value
    return [b0[0::4], b0[1::4], b1[0::4], b1[1::4]] + rows


def _bc3_alpha_streams(alpha):
    """8 byte streams of the DXT5 alpha block: a0 = max, a1 = min, 16 x 3 bit indices (the 8 value mode)."""
    size = alpha.size
    subs = _sub16(alpha)
    amin = amax = subs[0]
    for s in subs[1:]:
        amin = ImageChops.darker(amin, s)
        amax = ImageChops.lighter(amax, s)
    lv = _EV(lambda a: a["int"]((a["x"] - a["lo"]) * 7.0 / a["max"](a["hi"] - a["lo"], 1) + 0.5),
             x=alpha.convert("F"), lo=_up(amin, size).convert("F"), hi=_up(amax, size).convert("F"))
    s = [x.tobytes() for x in _sub16(lv.convert("L").point(_LUT_BC3))]
    out = [amax.tobytes(), amin.tobytes()]
    for k in (0, 8):
        out.append(_or(s[k], s[k + 1].translate(_SHL[3]), s[k + 2].translate(_TAB["and3shl6"])))
        out.append(_or(s[k + 2].translate(_TAB["shr2"]), s[k + 3].translate(_SHL[1]), s[k + 4].translate(_SHL[4]), s[k + 5].translate(_TAB["and1shl7"])))
        out.append(_or(s[k + 5].translate(_TAB["shr1"]), s[k + 6].translate(_SHL[2]), s[k + 7].translate(_SHL[5])))
    return out


def _bc2_alpha_streams(alpha):
    """8 byte streams of the DXT3 alpha block: 16 x 4 bit alpha, the low nibble first."""
    s = [x.tobytes() for x in _sub16(alpha.point(_LUT_A4))]
    return [_or(s[2 * k], s[2 * k + 1].translate(_SHL[4])) for k in range(8)]


def _interleave(streams):
    n = len(streams[0])
    out = bytearray(len(streams) * n)
    for i, s in enumerate(streams):
        out[i::len(streams)] = s
    return bytes(out)


def encode_level(rgb, alpha, fmt, refine=1):
    """The data of one mip level in the DDS format fmt (alpha: L image or None for opaque)."""
    if fmt == "R8G8B8":
        return rgb.tobytes("raw", "BGR")
    if fmt == "A8R8G8B8":
        return merge_rgba(rgb, alpha if alpha is not None else Image.new("L", rgb.size, 255)).tobytes("raw", "BGRA")
    rgb = pad4(rgb)
    color = _bc1_color_streams(rgb, refine)
    if fmt == "DXT1":
        return _interleave(color)
    alpha = pad4(alpha) if alpha is not None else Image.new("L", rgb.size, 255)
    if fmt == "DXT3":
        return _interleave(_bc2_alpha_streams(alpha) + color)
    if fmt == "DXT5":
        return _interleave(_bc3_alpha_streams(alpha) + color)
    raise ValueError("cannot write " + fmt)


def level_bytes(fmt, w, h):
    if fmt == "R8G8B8":
        return w * h * 3
    if fmt == "A8R8G8B8":
        return w * h * 4
    return ((w + 3) // 4) * ((h + 3) // 4) * (8 if fmt == "DXT1" else 16)


def mip_sizes(w, h):
    """Sizes of a full mip chain, as Direct3D makes them."""
    out = [(w, h)]
    while w > 1 or h > 1:
        w, h = max(1, w >> 1), max(1, h >> 1)
        out.append((w, h))
    return out


def dds_header(fmt, w, h, levels):
    """The same header fields the game's own mip-mapped DDS files have (flags 0xA1007, caps 0x401008)."""
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000 | (0x20000 if levels > 1 else 0)
    caps = 0x1000 | (0x400008 if levels > 1 else 0)
    if fmt in ("DXT1", "DXT3", "DXT5"):
        pf = struct.pack("<II4s5I", 32, 0x4, fmt.encode("latin1"), 0, 0, 0, 0, 0)
    elif fmt == "A8R8G8B8":
        pf = struct.pack("<II4s5I", 32, 0x41, b"\0\0\0\0", 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000)
    elif fmt == "R8G8B8":
        pf = struct.pack("<II4s5I", 32, 0x40, b"\0\0\0\0", 24, 0xFF0000, 0xFF00, 0xFF, 0)
    else:
        raise ValueError("cannot write " + fmt)
    return b"DDS " + struct.pack("<7I44x", 124, flags, h, w, level_bytes(fmt, w, h), 0, levels if levels > 1 else 0) + pf + struct.pack("<4I4x", caps, 0, 0, 0)


def build_dds(rgb, alpha, fmt, refine=1):
    """A complete DDS file with a full mip chain. Mips: box filter, colour and alpha separately (the colour of
    transparent pixels has to be bled in before, see bleed)."""
    sizes = mip_sizes(*rgb.size)
    parts = [dds_header(fmt, rgb.width, rgb.height, len(sizes))]
    for i, size in enumerate(sizes):
        if i:
            rgb = rgb.resize(size, Image.BOX)
            if alpha is not None:
                alpha = alpha.resize(size, Image.BOX)
        data = encode_level(rgb, alpha, fmt, refine)
        if len(data) != level_bytes(fmt, *size):
            raise RuntimeError("internal: level %d of %s has %d bytes" % (i, fmt, len(data)))
        parts.append(data)
    return b"".join(parts)


# ---------------------------------------------------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------------------------------------------------
class Work:
    def __init__(self, args):
        self.root = os.path.abspath(args.work)
        self.data = os.path.abspath(args.data)
        self.src = os.path.join(self.root, "src")
        self.manifest_path = os.path.join(self.root, "manifest.json")
        self.overrides_path = os.path.join(self.root, "overrides.json")
        self.manifest = None
        self._pictures = None

    def load(self):
        if not os.path.isfile(self.manifest_path):
            log("no manifest at %s - run 'extract' first" % self.manifest_path)
            sys.exit(2)
        with open(self.manifest_path, "r", encoding="utf-8") as f:
            self.manifest = json.load(f)
        return self.manifest

    def src_png(self, name):
        return os.path.join(self.src, *self.manifest["textures"][name]["png"].split("/"))

    def pictures(self):
        """The 2D background pictures (in the manifest since version 2; worked out from src for an older one)."""
        if self._pictures is None:
            self._pictures = self.manifest.get("pictures") or find_pictures(self.manifest, lambda n: Image.open(self.src_png(n)))
        return self._pictures

    def overrides(self):
        """{texture name: {"method": ..., "model": ..., "note": ...}}, kept by the 'override' command."""
        if not os.path.isfile(self.overrides_path):
            return {}
        with open(self.overrides_path, "r", encoding="utf-8") as f:
            return json.load(f).get("names", {})


def category_of(name, gui_refs):
    low = name.replace("/", "\\").lower()
    parts = low.split("\\")
    if parts[-1].startswith("bkg_"):
        return "bkg"
    if (len(parts) > 1 and parts[0] in GUI_FOLDERS) or low in gui_refs:
        return "hud"
    if len(parts) > 2 and parts[0] == "char" and parts[1] == "lighting":
        return "lut"  # 32 / 64 pixel light ramps of the skin shader (ASkinLightingManager): look-up data, not pictures
    return "world"


def add_select_args(p):
    p.add_argument("--only", action="append", default=[], metavar="GLOB", help="names matching the pattern (repeatable)")
    p.add_argument("--include-hud", action="store_true", help="also hud / fonts / dialogs / GUI section textures")
    p.add_argument("--include-bkg", action="store_true", help="also the bkg_* 2D background tiles")
    p.add_argument("--no-localized-bkg", action="store_true", help="leave out the copies of background pictures for other languages (<picture>D / E / F / I / N)")
    p.add_argument("--include-lut", action="store_true", help="also the light ramps of the skin shader (Char\\Lighting\\*)")
    p.add_argument("--include-conflicts", action="store_true", help="also names with different pictures in different libraries")
    p.add_argument("--sample", type=int, default=0, metavar="N", help="a sample of N spread over formats and categories")
    p.add_argument("--limit", type=int, default=0, metavar="N", help="the first N of the selection")


def select(man, a, pictures=None):
    """Names chosen by the selection switches, sorted. With pictures (Work.pictures()) the tiles of background
    pictures without a clean tile grid are left out and listed: they cannot be upscaled as whole pictures."""
    pats = [p.replace("/", "\\").lower() for p in a.only]
    out = []
    left_out = {}
    unclean = set()
    tile_pic = {n: p for p, e in (pictures or {}).items() for n in e["tiles"]}
    # AGUISlideshow::LocaliseBackgroundFileName: outside English the game loads <picture> + the first letter of the
    # language's own name (Deutsch, Espanol, Francais, Italiano, Nederlands), so English never shows those copies
    picture_of = lambda n: re.sub(r"_\d+\.tga$", "", n, flags=re.I).lower()
    all_pictures = {picture_of(n) for n, t in man["textures"].items() if t["category"] == "bkg"} if getattr(a, "no_localized_bkg", False) else None
    localized = set()
    for name in sorted(man["textures"], key=str.lower):
        t = man["textures"][name]
        if pats and not any(fnmatch.fnmatchcase(name.lower(), p) for p in pats):
            continue
        why = None
        if t["category"] == "bkg" and not a.include_bkg:
            why = "--include-bkg"
        elif t["category"] == "hud" and not a.include_hud:
            why = "--include-hud"
        elif t["category"] == "lut" and not a.include_lut:
            why = "--include-lut"
        elif t["differs"] and t["copies"]["kind"] == "content" and not a.include_conflicts:
            why = "--include-conflicts"
        if why:
            left_out[why] = left_out.get(why, 0) + 1
            continue
        if t["category"] == "bkg" and all_pictures is not None and picture_of(name)[-1:] in "defin" and picture_of(name)[:-1] in all_pictures:
            localized.add(picture_of(name))
            continue
        if t["category"] == "bkg" and pictures is not None and not pictures[tile_pic[name]]["clean"]:
            unclean.add(tile_pic[name])
            continue
        out.append(name)
    if pats and left_out:
        log("note: --only also matches " + ", ".join("%d names that need %s" % (v, k) for k, v in sorted(left_out.items())))
    if localized:
        log("note: %d background pictures for other languages are left out (--no-localized-bkg)" % len(localized))
    if unclean:
        log("note: %d background pictures are left out, their tiles do not form the clean 3 x 2 grid ('stats' says why): %s"
            % (len(unclean), ", ".join(sorted(unclean, key=str.lower))))
    if a.sample and a.sample < len(out):
        groups = {}
        for n in out:
            t = man["textures"][n]
            kind = "opaque" if not t["alpha"]["used"] else ("binary" if t["alpha"]["binary"] else "soft")
            groups.setdefault((t["category"], t["out_format"], kind), []).append(n)
        keys = sorted(groups)
        picked = set()
        for k in keys:  # one of every kind first
            if len(picked) < a.sample:
                picked.add(groups[k][len(groups[k]) // 2])
        rest = [n for n in out if n not in picked]
        need = a.sample - len(picked)
        if need > 0 and rest:
            step = len(rest) / float(need)
            picked.update(rest[min(len(rest) - 1, int(i * step + step / 2))] for i in range(need))
        out = [n for n in out if n in picked]
    if a.limit:
        out = out[:a.limit]
    return out


# ---------------------------------------------------------------------------------------------------------------------
# extract
# ---------------------------------------------------------------------------------------------------------------------
LIB_NORMAL, LIB_LOCALIZED, LIB_16BIT = 0, 1, 2
LIB_KIND_NAMES = {LIB_NORMAL: "normal", LIB_LOCALIZED: "localized", LIB_16BIT: "16-bit device"}


def library_kind(stem, loose_stems):
    """gui_Loc_<LANG>.ttl replaces gui_Loc.ttl in other languages; <name>16.ttl is only loaded by devices without
    32 bit textures (AMaterialLibraryManager::LoadLibrary)."""
    s = stem.lower()
    if re.match(r"^gui_loc16(_[a-z]{3})?$", s) or (s.endswith("16") and s[:-2] in loose_stems):
        return LIB_16BIT
    if re.match(r"^gui_loc_[a-z]{3}$", s):
        return LIB_LOCALIZED
    return LIB_NORMAL


def cmd_extract(a, work):
    t0 = time.time()
    low_priority()
    if not os.path.isdir(work.data):
        log("no data folder:", work.data)
        return 2
    files = []
    for dp, dn, fn in os.walk(work.data):
        dn.sort(key=str.lower)
        for f in sorted(fn, key=str.lower):
            if f.lower().endswith((".ttl", ".trb")):
                files.append(os.path.join(dp, f))
    loose_stems = {os.path.splitext(os.path.basename(f))[0].lower() for f in files if f.lower().endswith(".ttl")}
    log("scanning %d .ttl / .trb files under %s" % (len(files), work.data))

    variants = {}   # name.lower() -> {md5: {"name", "dds", "info", "libs": [(lib id, kind)]}}
    libraries = []  # (lib id, kind, texture count)
    skipped = []
    gui_refs = {}
    entries = read_bytes = 0
    for n, path in enumerate(files):
        rel = os.path.relpath(path, work.data).replace("\\", "/")
        with open(path, "rb") as f:
            d = f.read()
        read_bytes += len(d)
        try:
            trb = TRB(d)
        except ValueError:
            continue
        for sym, sec, off in trb.symbols:
            low = sym.lower()
            try:
                if low.endswith("texturesections"):
                    for k, v in read_texture_sections(trb, sec, off).items():
                        gui_refs[k] = max(gui_refs.get(k, 0), v)
                    continue
                if low != "ttl" and not low.endswith("_ttl"):
                    continue
                # a loose library is the file, a library inside an asset pack is the symbol <name>_TTL
                lib = rel if low == "ttl" else rel + "::" + sym
                kind = library_kind(os.path.splitext(os.path.basename(path))[0] if low == "ttl" else sym[:-4], loose_stems)
                textures = read_ttl(trb, sec, off)
            except (ValueError, struct.error, IndexError) as e:
                skipped.append({"library": rel + "::" + sym, "reason": str(e)})
                continue
            libraries.append((lib, kind, len(textures)))
            for name, dds in textures:
                entries += 1
                md5 = hashlib.md5(dds).hexdigest()
                v = variants.setdefault(name.replace("/", "\\").lower(), {}).get(md5)
                if v is None:
                    v = variants[name.replace("/", "\\").lower()][md5] = {"name": name.replace("/", "\\"), "dds": dds, "libs": []}
                v["libs"].append((lib, kind))
        if (n + 1) % 1000 == 0:
            log("  %d / %d files, %d libraries, %d names" % (n + 1, len(files), len(libraries), len(variants)))
    t_scan = time.time() - t0
    log("read %s in %.1f s: %d libraries, %d texture entries, %d unique names; %d libraries skipped"
        % (fmt_bytes(read_bytes), t_scan, len(libraries), entries, len(variants), len(skipped)))
    for s in skipped:
        log("  skipped %s: %s" % (s["library"], s["reason"]))

    os.makedirs(work.src, exist_ok=True)
    textures = {}
    used_png = set()
    counts = {}
    for i, key in enumerate(sorted(variants)):
        vs = variants[key]
        for md5, v in vs.items():
            v["md5"] = md5
            v["info"] = dds_info(v["dds"])
            v["uses"] = len(v["libs"])
            v["kind"] = min(k for _, k in v["libs"])  # the most ordinary library it is in
        order = sorted(vs.values(), key=lambda v: (-v["info"]["width"] * v["info"]["height"], v["kind"],
                                                   -BIT_RANK.get(v["info"]["format"], 1), -v["uses"], v["md5"]))
        kept = order[0]
        info = kept["info"]
        im = decode_top(kept["dds"], info)
        rgb, alpha = split_rgba(im)
        lo, hi = alpha.getextrema() if alpha is not None else (255, 255)
        hist = alpha.histogram() if alpha is not None else None
        used = lo < 255
        binary = bool(used and not any(hist[1:255]))
        if not used:
            im = rgb
        name = kept["name"]
        cat = category_of(name, gui_refs)
        seams = seam_stats(im)
        wrap = [False, False] if cat == "bkg" else [tiles(seams[0], seams[1]), tiles(seams[2], seams[3])]

        png = os.path.splitext(name)[0].replace("\\", "/") + ".png"
        if png.lower() in used_png:
            png = name.replace("\\", "/") + ".png"
        used_png.add(png.lower())
        save_png(im, os.path.join(work.src, *png.split("/")))

        t = {
            "png": png, "category": cat, "gui_sections": gui_refs.get(key, 0),
            "format": info["format"], "out_format": out_format(info["format"], used),
            "width": info["width"], "height": info["height"], "mips": info["mips"], "dds_bytes": len(kept["dds"]), "md5": kept["md5"],
            "library": sorted(l for l, _ in kept["libs"])[0], "library_kind": LIB_KIND_NAMES[kept["kind"]],
            "uses": sum(v["uses"] for v in vs.values()),
            "alpha": {"channel": alpha is not None, "used": used, "binary": binary, "min": lo, "max": hi},
            "wrap": wrap, "seam": seams, "differs": len(vs) > 1,
        }
        if len(vs) > 1:
            small = lambda x: merge_rgba(*[c.resize((32, 32), Image.BOX) if c is not None else Image.new("L", (32, 32), 255)
                                          for c in split_rgba(x)])
            ref = small(im if im.mode == "RGBA" else im.convert("RGBA"))
            worst, same_pixels, vlist = 0.0, True, []
            for v in order:
                d = 0.0
                if v is not kept:
                    other = decode_top(v["dds"], v["info"]).convert("RGBA")
                    d = mean_abs_diff(ref, small(other))
                    worst = max(worst, d)
                    if other.size != im.size or other.tobytes() != (im if im.mode == "RGBA" else im.convert("RGBA")).tobytes():
                        same_pixels = False
                libs = sorted(l for l, _ in v["libs"])
                vlist.append({"md5": v["md5"], "format": v["info"]["format"], "width": v["info"]["width"], "height": v["info"]["height"],
                              "mips": v["info"]["mips"], "uses": v["uses"], "library_kind": LIB_KIND_NAMES[v["kind"]],
                              "example": libs[0], "diff_to_kept": round(d, 2), "kept": v is kept})
            sizes = {(v["info"]["width"], v["info"]["height"]) for v in order}
            if same_pixels:
                kind, why = "mips_only", "all copies have the same top level pixels, only the stored mip chain differs; kept the most used one"
            elif worst <= 16.0:
                kind = "lod"
                why = ("same picture in %d sizes / encodings; kept the largest (%dx%d %s)" % (len(vs), info["width"], info["height"], info["format"])
                       if len(sizes) > 1 else "same picture, slightly different pixels (other encoding); kept the %s copy" % ("higher bit depth / most used"))
            else:
                kind = "content"
                why = "DIFFERENT PICTURES share this name; kept the largest / %s / most used copy" % LIB_KIND_NAMES[kept["kind"]]
            if kept["kind"] != max(v["kind"] for v in order):
                why += "; copies from %s libraries were ranked below the normal ones" % " and ".join(
                    sorted({LIB_KIND_NAMES[v["kind"]] for v in order if v["kind"] != kept["kind"]}))
            t["copies"] = {"kind": kind, "kept": why, "max_diff_to_kept": round(worst, 2), "variants": vlist}
            counts["differs_" + kind] = counts.get("differs_" + kind, 0) + 1
        textures[name] = t
        counts[cat] = counts.get(cat, 0) + 1
        if (i + 1) % 500 == 0:
            log("  %d / %d textures written" % (i + 1, len(variants)))

    # PNG files of an earlier run that no texture owns any more
    stale = 0
    for dp, _dn, fn in os.walk(work.src):
        for f in fn:
            relp = os.path.relpath(os.path.join(dp, f), work.src).replace("\\", "/").lower()
            if f.lower().endswith((".png", ".tmp")) and relp not in used_png:
                os.remove(os.path.join(dp, f))
                stale += 1

    pictures = find_pictures({"textures": textures}, lambda n: Image.open(os.path.join(work.src, *textures[n]["png"].split("/"))))
    for pic, e in pictures.items():
        for n in e["tiles"]:
            textures[n]["picture"] = pic

    manifest = {
        "tool": "hd_textures.py", "version": VERSION, "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "data_root": work.data, "files_scanned": len(files), "libraries": len(libraries), "entries": entries,
        "names": len(textures), "counts": counts,
        "library_kinds": {LIB_KIND_NAMES[k]: sum(1 for l in libraries if l[1] == k) for k in LIB_KIND_NAMES},
        "skipped_libraries": skipped,
        "notes": {
            "category": "bkg = tile of a 2D background picture (bkg_*); hud = hud/fonts/dialogs folder or cut up by GUI texture sections; lut = light ramps of the skin shader (Char/Lighting); world = everything else",
            "out_format": "what pack / testpack write: the same format class as the original (pack: DXT1 for bkg tiles unless --bkg-format says otherwise)",
            "wrap": "[x, y]: the picture continues across that edge when repeated (heuristic on 'seam' = [seam x, inside x, seam y, inside y]); decides the padding before upscaling",
            "differs": "true when libraries hold different bytes under this name; 'copies' says which copy was kept and why",
            "pictures": "2D background pictures: tiles 1..6 = 3 columns x 2 rows sharing one pixel column / row; clean = can be put together, upscaled as a whole and cut again; why = the reason when not",
        },
        "pictures": pictures,
        "textures": textures,
    }
    write_atomic(work.manifest_path, json.dumps(manifest, indent=1, sort_keys=False).encode("utf-8"))
    total = sum(os.path.getsize(os.path.join(dp, f)) for dp, _dn, fn in os.walk(work.src) for f in fn)
    log("extract done in %.1f s (scan %.1f s): %d names -> %s (%s)%s" % (time.time() - t0, t_scan, len(textures), work.src, fmt_bytes(total),
                                                                         ", %d stale files removed" % stale if stale else ""))
    log("  categories:", {k: v for k, v in sorted(counts.items()) if not k.startswith("differs_")})
    log("  names with different bytes in different libraries:", {k[8:]: v for k, v in sorted(counts.items()) if k.startswith("differs_")})
    log("  background pictures: %d, %d of them with a clean tile grid" % (len(pictures), sum(e["clean"] for e in pictures.values())))
    log("  manifest:", work.manifest_path)
    return 0


def cmd_stats(a, work):
    man = work.load()
    tex = man["textures"]
    log("manifest of %s: %d names from %d libraries (%d entries), data %s" % (man["created"], len(tex), man["libraries"], man["entries"], man["data_root"]))
    table = {}
    for n, t in tex.items():
        kind = "opaque" if not t["alpha"]["used"] else ("binary alpha" if t["alpha"]["binary"] else "soft alpha")
        k = (t["category"], t["format"], t["out_format"], kind)
        e = table.setdefault(k, [0, 0, 0])
        e[0] += 1
        e[1] += t["width"] * t["height"]
        e[2] += t["dds_bytes"]
    log("%-6s %-9s %-9s %-13s %6s %9s %10s" % ("categ.", "format", "written", "alpha", "names", "Mpixel", "DDS bytes"))
    for k in sorted(table):
        e = table[k]
        log("%-6s %-9s %-9s %-13s %6d %9.2f %10s" % (k + (e[0], e[1] / 1e6, fmt_bytes(e[2]))))
    for cat in ("world", "lut", "hud", "bkg"):
        ts = [t for t in tex.values() if t["category"] == cat]
        sides = {}
        for t in ts:
            s = max(t["width"], t["height"])
            sides[s] = sides.get(s, 0) + 1
        log("%s: %d names, longest side: %s, wrap x/y: %d/%d" % (cat, len(ts), dict(sorted(sides.items())), sum(t["wrap"][0] for t in ts), sum(t["wrap"][1] for t in ts)))
    for kind in ("content", "lod", "mips_only"):
        names = sorted(n for n, t in tex.items() if t["differs"] and t["copies"]["kind"] == kind)
        log("differs / %s: %d%s" % (kind, len(names), (" -> " + ", ".join(names)) if kind == "content" else ""))
    if man["skipped_libraries"]:
        log("skipped libraries:", "; ".join("%s (%s)" % (s["library"], s["reason"]) for s in man["skipped_libraries"]))
    pictures = work.pictures()
    log("background pictures: %d, %d with a clean 3 x 2 tile grid (upscaled as whole pictures)" % (len(pictures), sum(e["clean"] for e in pictures.values())))
    reasons = {}
    for pic, e in pictures.items():
        if not e["clean"]:
            reasons.setdefault(re.sub(r"\d+\.\d+\)$", "..)", e["why"]), []).append(pic)
    for why, pics in sorted(reasons.items()):
        log("  left out, %s: %s" % (why, ", ".join(pics)))
    ov = work.overrides()
    if ov:
        kinds = {}
        for n, o in ov.items():
            k = o.get("method", "model") + (" " + o["model"] if o.get("model") else "")
            kinds[k] = kinds.get(k, 0) + 1
        log("overrides (%s): %s" % (work.overrides_path, ", ".join("%d x %s" % (v, k) for k, v in sorted(kinds.items()))))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# upscale
# ---------------------------------------------------------------------------------------------------------------------
def up_png(up_dir, t):
    return os.path.join(up_dir, *t["png"].split("/"))


def read_png_info(path):
    try:
        with Image.open(path) as im:
            return im.size, json.loads(im.text.get("hd_textures", "{}"))
    except Exception:
        return None, {}


AUTO_TILE = 200  # the tile size the exe picks by itself on a GPU with 2 GB or more. Files written by version 1 of this
#                  tool (no "one_tile" in their PNG text) were upscaled in tiles of that size.


def run_exe(a, model, tile, jobs, in_dir, out_dir, logf):
    exe = os.path.abspath(a.exe)
    cmd = [exe, "-i", in_dir, "-o", out_dir, "-n", model, "-s", str(SCALE), "-f", "png", "-m", a.models, "-t", str(tile)]
    if a.gpu is not None:
        cmd += ["-g", str(a.gpu)]
    if jobs:
        cmd += ["-j", jobs]
    if a.tta:
        cmd += ["-x"]
    logf.write(("\n=== %s\n%s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), subprocess.list2cmdline(cmd))).encode("utf-8"))
    logf.flush()
    flags = BELOW_NORMAL_PRIORITY_CLASS if os.name == "nt" else 0
    return subprocess.run(cmd, cwd=os.path.dirname(exe), stdout=logf, stderr=subprocess.STDOUT, creationflags=flags).returncode


def make_units(man, names, pictures, overrides, a):
    """What goes through the upscaler as one image: a texture, or a whole background picture (its six tiles)."""
    tile_pic = {n: p for p, e in pictures.items() for n in e["tiles"]}
    units, seen = [], set()
    for n in names:
        t = man["textures"][n]
        if t["category"] == "bkg":
            pic = tile_pic[n]
            if pic in seen:
                continue
            seen.add(pic)
            u = {"key": pic, "pic": pic, "names": list(pictures[pic]["tiles"]), "size": PIC_SIZE}
        else:
            u = {"key": n, "pic": None, "names": [n], "size": (t["width"], t["height"])}
        ov = {} if a.fake else overrides.get(u["names"][0], {})
        method = ov.get("method", "model")
        u["exe"] = not a.fake and method != "lanczos"
        model = (ov.get("model") or a.model) if u["exe"] else ("fake-lanczos" if a.fake else "lanczos")
        u["job"] = {"model": model, "method": method, "pad": a.pad, "wrap": a.wrap, "tta": bool(a.tta) and u["exe"],
                    "alpha": a.alpha if u["exe"] else "bicubic"}
        u["psize"] = (u["size"][0] + 2 * a.pad, u["size"][1] + 2 * a.pad)
        u["one_tile"] = u["exe"] and max(u["psize"]) <= (a.tile or a.max_tile)
        units.append(u)
    return units


def unit_done(man, up_dir, u):
    """True when every file of the unit is there and was made the way it would be made now."""
    for n in u["names"]:
        t = man["textures"][n]
        path = up_png(up_dir, t)
        if not os.path.isfile(path):
            return False
        size, info = read_png_info(path)
        if size != (t["width"] * SCALE, t["height"] * SCALE) or info.get("src_md5") != t["md5"] or info.get("picture") != u["pic"]:
            return False
        if any(info.get(k, "model" if k == "method" else None) != v for k, v in u["job"].items()):
            return False
        if u["one_tile"] and not info.get("one_tile", max(u["psize"]) <= AUTO_TILE):
            return False  # it was upscaled in several tiles: the model draws different detail in each of them
    return True


def unit_input(work, man, u, a):
    """(RGB with the padding, alpha with the padding or None, the wrap used, the source images)."""
    srcs = [Image.open(work.src_png(n)) for n in u["names"]]
    if u["pic"]:
        rgb, alpha, wrap = assemble_picture(srcs), None, [False, False]
    else:
        rgb, alpha = split_rgba(srcs[0])
        wrap = {"auto": man["textures"][u["names"][0]]["wrap"], "always": [True, True], "never": [False, False]}[a.wrap]
    prgb = pad_image(rgb, a.pad, wrap)
    palpha = pad_image(alpha, a.pad, wrap) if alpha is not None else None
    if palpha is not None:
        prgb = bleed(prgb, palpha)
    return prgb, palpha, wrap, srcs


def batches(units, count, mpx):
    """Runs of at most count units and about mpx megapixels (the exe is started once per run)."""
    cur, px = [], 0
    for u in units:
        p = u["psize"][0] * u["psize"][1]
        if cur and (len(cur) >= count or px + p > mpx * 1e6):
            yield cur
            cur, px = [], 0
        cur.append(u)
        px += p
    if cur:
        yield cur


def cmd_upscale(a, work):
    t0 = time.time()
    low_priority()
    man = work.load()
    up_dir = os.path.abspath(a.up or os.path.join(work.root, "up_fake" if a.fake else "up"))
    stage_in, stage_out = os.path.join(work.root, "stage", "in"), os.path.join(work.root, "stage", "out")
    pictures = work.pictures()
    names = select(man, a, pictures)
    units = make_units(man, names, pictures, {} if a.no_overrides else work.overrides(), a)
    todo = [u for u in units if a.force or not unit_done(man, up_dir, u)]
    need = sorted({u["job"]["model"] or "" for u in todo if u["exe"]})
    if need:
        if not a.exe or not os.path.isfile(a.exe):
            log("upscale needs --exe <realesrgan-ncnn-vulkan.exe> (or --fake)" + (": no such file %s" % a.exe if a.exe else ""))
            return 2
        if "" in need:
            log("upscale needs --model (or --fake)")
            return 2
        a.models = os.path.abspath(a.models or os.path.join(os.path.dirname(os.path.abspath(a.exe)), "models"))
        have = sorted(f[:-6] for f in os.listdir(a.models) if f.lower().endswith(".param")) if os.path.isdir(a.models) else []
        for m in need:
            if m not in have and "%s-x%d" % (m, SCALE) not in have:
                log("model '%s' not found in %s; there: %s" % (m, a.models, ", ".join(have) or "(nothing)"))
                return 2
    mpx = sum(u["psize"][0] * u["psize"][1] for u in todo) / 1e6
    kinds = {}
    for u in units:
        k = "%s%s" % (u["job"]["model"], " + consistent" if u["job"]["method"] == "consistent" else "")
        kinds[k] = kinds.get(k, 0) + 1
    log("upscale -> %s: %d names = %d images (%d of them whole background pictures), %d already there, %d to do = %.1f Mpixel in, %.0f Mpixel out"
        % (up_dir, len(names), len(units), sum(1 for u in units if u["pic"]), len(units) - len(todo), len(todo), mpx, mpx * SCALE * SCALE))
    log("  made by: %s (pad %d, alpha %s)" % (", ".join("%s (%d)" % kv for kv in sorted(kinds.items())) or "-", a.pad, a.alpha))
    if not todo:
        return 0
    os.makedirs(up_dir, exist_ok=True)
    groups = {}
    for u in todo:
        groups.setdefault(u["job"]["model"] if u["exe"] else "", []).append(u)
    done, failed = 0, []
    logf = open(os.path.join(work.root, "upscale_exe.log"), "ab") if need else None
    try:
        for model in sorted(groups):
            ordered = sorted(groups[model], key=lambda u: (max(u["psize"]), u["key"].lower()))
            for batch in batches(ordered, a.batch, a.batch_mpx):
                tb = time.time()
                inputs = [unit_input(work, man, u, a) for u in batch]
                rc = tile = 0
                if model:
                    for d in (stage_in, stage_out):
                        shutil.rmtree(d, ignore_errors=True)
                        os.makedirs(d, exist_ok=True)
                        if os.listdir(d):
                            raise RuntimeError("cannot empty %s (is a file in it open in another program?)" % d)
                    for i, (prgb, palpha, _wrap, _srcs) in enumerate(inputs):
                        prgb.save(os.path.join(stage_in, "t%06d.png" % i), compress_level=1)
                        if palpha is not None and a.alpha == "model":
                            palpha.convert("RGB").save(os.path.join(stage_in, "t%06d_a.png" % i), compress_level=1)
                    # One tile per image wherever it fits (see unit_done). Two big tiles at once do not fit into
                    # the GPU memory (vkAllocateMemory fails and the exe dies), so those go one at a time.
                    tile = a.tile or max(32, min(a.max_tile, max(max(u["psize"]) for u in batch)))
                    rc = run_exe(a, model, tile, a.jobs or ("1:1:2" if tile > 320 else ""), stage_in, stage_out, logf)
                for i, (u, (prgb, palpha, wrap, srcs)) in enumerate(zip(batch, inputs)):
                    try:
                        full = (prgb.width * SCALE, prgb.height * SCALE)
                        malpha = None
                        if not model:
                            out = lanczos_up(prgb, sharpen=not a.fake)
                        else:
                            stem = os.path.join(stage_out, "t%06d" % i)
                            if not os.path.isfile(stem + ".png"):
                                raise ValueError("the upscaler wrote no output for it (see upscale_exe.log)")
                            out = Image.open(stem + ".png").convert("RGB")
                            if out.size != full:
                                raise ValueError("output is %dx%d, expected %dx%d" % (out.size + full))
                            if not a.no_sanity:
                                d = mean_abs_diff(out.resize(prgb.size, Image.BOX), prgb)
                                if d > 24.0:
                                    raise ValueError("output does not look like the input (mean difference %.1f)" % d)
                            if palpha is not None and a.alpha == "model":
                                malpha = Image.open(stem + "_a.png").convert("L")
                                if malpha.size != full:
                                    raise ValueError("alpha output has the wrong size")
                            if u["job"]["method"] == "consistent":
                                out = back_project(out, prgb)
                        p = a.pad * SCALE
                        box = (p, p, full[0] - p, full[1] - p)
                        out = out.crop(box)
                        oalpha = None
                        if palpha is not None:
                            oalpha = (malpha if malpha is not None else palpha.resize(full, Image.BICUBIC)).crop(box)
                        images = cut_picture(out, srcs) if u["pic"] else [merge_rgba(out, oalpha)]
                        info = dict(u["job"], wrap_used=wrap, tile=tile, one_tile=bool(not model or max(prgb.size) <= tile))
                        if u["pic"]:
                            info["picture"] = u["pic"]
                        for n, im in zip(u["names"], images):
                            t = man["textures"][n]
                            if im.size != (t["width"] * SCALE, t["height"] * SCALE):
                                raise ValueError("internal: %dx%d made for %s" % (im.size + (n,)))
                        for n, im in zip(u["names"], images):
                            t = man["textures"][n]
                            save_png(im, up_png(up_dir, t), dict(info, src_md5=t["md5"], name=n))
                            done += 1
                    except Exception as e:
                        failed.append((u["key"], str(e)))
                log("  %s: %d images up to %dx%d%s in %.1f s%s; %d files written, %d images failed so far"
                    % (model or batch[0]["job"]["model"], len(batch), batch[-1]["psize"][0], batch[-1]["psize"][1],
                       ", -t %d" % tile if model else "", time.time() - tb,
                       "" if rc == 0 else " (exe returned %d, see upscale_exe.log)" % rc, done, len(failed)))
    finally:
        if logf is not None:
            logf.close()
        shutil.rmtree(os.path.join(work.root, "stage"), ignore_errors=True)
    for key, why in failed[:40]:
        log("  FAILED %s: %s" % (key, why))
    log("upscale done in %.1f s: %d files written, %d images kept, %d images failed" % (time.time() - t0, done, len(units) - len(todo), len(failed)))
    return 1 if failed else 0


# ---------------------------------------------------------------------------------------------------------------------
# override: per-name exceptions for upscale, kept in <work>\overrides.json so that every later run makes the same choice
#   model       the model alone (the default); with --model another model than the one of the upscale command
#   consistent  the model's picture, corrected so that it is the original again when reduced to the original size
#               (see back_project): for grain the model smoothed away and for shapes it invented
#   lanczos     no model: Lanczos with a mild unsharp mask, for small glyphs and icons the models damage
# ---------------------------------------------------------------------------------------------------------------------
def cmd_override(a, work):
    man = work.load()
    ov = work.overrides()
    if not a.patterns:
        if a.method or a.clear:
            log("override: which names? give one or more patterns, e.g. \"World\\drt*\"")
            return 2
        for n in sorted(ov, key=str.lower):
            o = ov[n]
            log("%-44s %s%s%s" % (n, o.get("method", "model"), " " + o["model"] if o.get("model") else "", "   # " + o["note"] if o.get("note") else ""))
        log("%d overrides in %s" % (len(ov), work.overrides_path))
        return 0
    if bool(a.method) == bool(a.clear):
        log("override: give either --method or --clear")
        return 2
    pats = [p.replace("/", "\\").lower() for p in a.patterns]
    pictures = work.pictures()
    tile_pic = {n: p for p, e in pictures.items() for n in e["tiles"]}
    names = []
    for n in sorted(man["textures"], key=str.lower):
        if any(fnmatch.fnmatchcase(n.lower(), p) for p in pats):
            names += pictures[tile_pic[n]]["tiles"] if n in tile_pic else [n]  # a background picture is one image
    names = list(dict.fromkeys(names))
    if not names:
        log("override: no texture name matches")
        return 2
    for n in names:
        if a.clear:
            ov.pop(n, None)
        else:
            o = ov[n] = {"method": a.method}
            if a.model and a.method != "lanczos":
                o["model"] = a.model
            if a.note:
                o["note"] = a.note
    doc = {"tool": "hd_textures.py", "what": "per-name exceptions for 'upscale' (see cmd_override); edit with the 'override' command",
           "names": {n: ov[n] for n in sorted(ov, key=str.lower)}}
    write_atomic(work.overrides_path, json.dumps(doc, indent=1).encode("utf-8"))
    log("override: %d names %s; %d overrides in %s. Run 'upscale' again: it redoes exactly what changed."
        % (len(names), "cleared" if a.clear else "set to " + a.method + (" " + a.model if a.model and a.method != "lanczos" else ""), len(ov), work.overrides_path))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# pack / testpack
# ---------------------------------------------------------------------------------------------------------------------
def out_path(out_dir, name):
    return os.path.join(out_dir, *name.split("\\"))


def expected_file_bytes(fmt, w, h):
    return 128 + sum(level_bytes(fmt, *s) for s in mip_sizes(w, h))


def write_pack(a, work, names, make, label):
    """make(name, t) -> (rgb, alpha) or (rgb, alpha, format) at the size to write, or None to skip."""
    out_dir = os.path.abspath(a.out)
    os.makedirs(out_dir, exist_ok=True)
    man = work.manifest
    written = total = 0
    mine = set()
    problems = []
    formats = {}
    t0 = time.time()
    for i, n in enumerate(names):
        t = man["textures"][n]
        path = out_path(out_dir, n)
        try:
            res = make(n, t)
            if res is None:
                continue
            rgb, alpha = res[:2]
            fmt = res[2] if len(res) > 2 else t["out_format"]
            formats[fmt] = formats.get(fmt, 0) + 1
            data = build_dds(rgb, alpha, fmt, a.refine)
            if len(data) != expected_file_bytes(fmt, *rgb.size):
                raise RuntimeError("internal: %d bytes, expected %d" % (len(data), expected_file_bytes(fmt, *rgb.size)))
            write_atomic(path, data)
            mine.add(os.path.normcase(path))
            written += 1
            total += len(data)
        except Exception as e:
            problems.append((n, str(e)))
        if (i + 1) % 250 == 0:
            log("  %d / %d" % (i + 1, len(names)))
    # files of earlier runs that this run did not write
    others = [os.path.join(dp, f) for dp, _dn, fn in os.walk(out_dir) for f in fn if os.path.normcase(os.path.join(dp, f)) not in mine]
    if others and a.clean:
        for p in others:
            os.remove(p)
        for dp, dn, fn in os.walk(out_dir, topdown=False):
            if dp != out_dir and not os.listdir(dp):
                os.rmdir(dp)
    for n, why in problems[:40]:
        log("  FAILED %s: %s" % (n, why))
    log("%s -> %s: %d files, %d bytes (%s), %d problems, %.1f s" % (label, out_dir, written, total, fmt_bytes(total), len(problems), time.time() - t0))
    log("  formats: %s" % ", ".join("%s (%d)" % kv for kv in sorted(formats.items())))
    if others:
        log("  %d other files in the folder %s" % (len(others), "were removed (--clean)" if a.clean else "are from earlier runs and were left alone (--clean removes them)"))
    return problems


def cmd_pack(a, work):
    low_priority()
    man = work.load()
    if a.factor not in (1, 2, 4):
        log("--factor must be 1, 2 or 4 (the upscale is %dx)" % SCALE)
        return 2
    up_dir = os.path.abspath(a.up or os.path.join(work.root, "up"))
    names = select(man, a, work.pictures())
    rgb_pats = [p.replace("/", "\\").lower() for p in a.bkg_rgb]
    missing = []
    seen_models = {}

    def make(n, t):
        path = up_png(up_dir, t)
        if not os.path.isfile(path):
            missing.append(n)
            return None
        im = Image.open(path)
        info = json.loads(getattr(im, "text", {}).get("hd_textures", "{}"))
        by = "%s%s" % (info.get("model", "?"), " + consistent" if info.get("method") == "consistent" else "")
        seen_models[by] = seen_models.get(by, 0) + 1
        if t["category"] == "bkg" and info.get("picture") is None:
            raise ValueError("this background tile was upscaled alone, not as part of its picture (re-run upscale)")
        if im.size != (t["width"] * SCALE, t["height"] * SCALE):
            raise ValueError("upscaled file is %dx%d, expected %dx%d" % (im.size + (t["width"] * SCALE, t["height"] * SCALE)))
        if info.get("src_md5") not in (None, t["md5"]):
            raise ValueError("upscaled file was made from another original (re-run upscale)")
        rgb, alpha = split_rgba(im)
        w, h = t["width"] * a.factor, t["height"] * a.factor
        while a.max_size and max(w, h) > a.max_size and min(w, h) > 4:
            w, h = w // 2, h // 2
        if (w, h) != rgb.size:
            rgb = rgb.resize((w, h), Image.BOX)
            alpha = alpha.resize((w, h), Image.BOX) if alpha is not None else None
        if not t["alpha"]["used"]:
            alpha = None
        elif alpha is None:
            raise ValueError("the original has alpha, the upscaled file has none")
        elif t["alpha"]["binary"] and not a.soft_alpha:
            alpha = alpha.point([0] * 128 + [255] * 128)  # a cut-out stays a cut-out
        # no bleed here: the colour under transparent pixels is the upscaled bled colour already
        if t["category"] == "bkg":
            # The originals are uncompressed; twice the size uncompressed is 5.2 MB per picture, DXT1 0.9 MB.
            # The game keeps the image info of the original, so the format of a replacement is free.
            fmt = a.bkg_format
            if any(fnmatch.fnmatchcase(n.lower(), p) for p in rgb_pats):
                fmt = "R8G8B8"
            elif fmt == "DXT1" and a.bkg_rgb_below:
                # what DXT1 would cost this tile, measured on the rows that hold picture
                dds = dds_header("DXT1", w, h, 1) + encode_level(rgb, None, "DXT1", a.refine)
                rows = h if re.search(r"_[123]\.tga$", n, re.I) else (PIC_SHOWN_ROWS - PIC_TILES[3][1]) * h // t["height"]
                st = ImageStat.Stat(ImageChops.difference(rgb.crop((0, 0, w, rows)), decode_top(dds, dds_info(dds)).convert("RGB").crop((0, 0, w, rows))))
                mse = sum(st.sum2) / (3.0 * w * rows)
                psnr = 99.0 if mse <= 0 else 10 * math.log10(65025.0 / mse)
                dxt_psnr.append(psnr)
                if psnr < a.bkg_rgb_below:
                    fmt = "R8G8B8"
            return rgb, None, fmt
        return rgb, alpha

    dxt_psnr = []
    problems = write_pack(a, work, names, make, "pack x%d" % a.factor)
    if dxt_psnr:
        dxt_psnr.sort()
        log("  background tiles as DXT1 against uncompressed: PSNR worst %.1f dB, 10 %% below %.1f, median %.1f; %d of %d are below %.1f dB and were written as R8G8B8"
            % (dxt_psnr[0], dxt_psnr[len(dxt_psnr) // 10], dxt_psnr[len(dxt_psnr) // 2], sum(p < a.bkg_rgb_below for p in dxt_psnr), len(dxt_psnr), a.bkg_rgb_below))
    log("  source %s, made by: %s" % (up_dir, ", ".join("%s (%d)" % kv for kv in sorted(seen_models.items())) or "-"))
    if "fake-lanczos" in seen_models:
        log("  NOTE: this pack contains FAKE (Lanczos) upscales - for testing the chain only")
    if missing:
        log("  %d selected textures have no upscaled file in %s%s" % (len(missing), up_dir, "" if a.allow_missing else " (run upscale, or pass --allow-missing)"))
        for n in missing[:12]:
            log("    missing:", n)
    return 1 if problems or (missing and not a.allow_missing) else 0


_TINT_UP = [128 + v // 2 for v in range(256)]
_TINT_DOWN = [v // 4 for v in range(256)]


def tint(rgb):
    """A strong magenta tint that keeps the detail: red and blue pushed up, green down."""
    r, g, b = rgb.split()
    return Image.merge("RGB", (r.point(_TINT_UP), g.point(_TINT_DOWN), b.point(_TINT_UP)))


def cmd_testpack(a, work):
    low_priority()
    man = work.load()
    a.refine = 1

    def make(n, t):
        rgb, alpha = split_rgba(Image.open(work.src_png(n)))
        rgb = tint(rgb)
        if not t["alpha"]["used"]:
            alpha = None
        if alpha is not None:
            rgb = bleed(rgb, alpha)
        return rgb, alpha

    problems = write_pack(a, work, select(man, a), make, "testpack (magenta, original size and format)")
    return 1 if problems else 0


# ---------------------------------------------------------------------------------------------------------------------
# review: numbers that tell where an upscale left the original. They pick what to LOOK at (--sheet), they are no verdict.
#   diff   mean difference between the upscale reduced to the original size and the original (0..255): how far the
#          picture was changed at the resolution the artists drew it in
#   lost   share of the original's pixel-to-pixel contrast that is gone at that size: grain and fine texture the
#          model smoothed away (plain dirt, grass, noise)
#   added  fine detail of the upscale relative to a plain Lanczos enlargement (1 = none added)
#   patchy how unevenly that detail is spread over the picture (detail drawn into one corner only)
# ---------------------------------------------------------------------------------------------------------------------
def _contrast(im, mask):
    """Mean absolute difference of neighbouring pixels (both axes, wrapped), over the visible pixels."""
    tot = 0.0
    for dx, dy in ((1, 0), (0, 1)):
        d = ImageChops.difference(im, ImageChops.offset(im, dx, dy)).convert("L")
        tot += ImageStat.Stat(d, mask).mean[0]
    return tot / 2


def review_metrics(src, up):
    s_rgb, s_a = split_rgba(src)
    u_rgb, _u_a = split_rgba(up)
    mask = None
    if s_a is not None and s_a.getextrema()[0] < 255:
        if s_a.getextrema()[1] == 0:
            return None  # nothing visible
        s_rgb = bleed(s_rgb, s_a)
        mask = s_a.point([0] * 16 + [255] * 240)
        if mask.getextrema()[1] == 0:
            mask = None
    down = u_rgb.resize(s_rgb.size, Image.BOX)
    diff = ImageStat.Stat(ImageChops.difference(down, s_rgb).convert("L"), mask).mean[0]
    c_src, c_down = _contrast(s_rgb, mask), _contrast(down, mask)
    lz = s_rgb.resize(u_rgb.size, Image.LANCZOS)
    cells = max(2, min(8, min(s_rgb.size) // 8))

    def fine(im):  # fine detail per cell of a cells x cells grid
        l = im.convert("L")
        return list(ImageChops.difference(l, l.filter(ImageFilter.GaussianBlur(3))).resize((cells, cells), Image.BOX).tobytes())

    ratios = [(x + 0.5) / (y + 0.5) for x, y in zip(fine(u_rgb), fine(lz))]
    mean = sum(ratios) / len(ratios)
    spread = (sum((r - mean) ** 2 for r in ratios) / len(ratios)) ** 0.5
    return {"diff": round(diff, 2), "lost": round(1.0 - c_down / max(c_src, 0.05), 3), "contrast": round(c_src, 2),
            "added": round(mean, 2), "patchy": round(spread / max(mean, 0.01), 2)}


def cmd_review(a, work):
    low_priority()
    man = work.load()
    up_dir = os.path.abspath(a.up or os.path.join(work.root, "up"))
    rows = []
    for n in select(man, a):
        t = man["textures"][n]
        if t["category"] == "bkg" or not os.path.isfile(up_png(up_dir, t)):
            continue
        up = Image.open(up_png(up_dir, t))
        info = json.loads(getattr(up, "text", {}).get("hd_textures", "{}"))
        m = review_metrics(Image.open(work.src_png(n)), up)
        if m is None:
            continue
        m["name"], m["size"] = n, "%dx%d" % (t["width"], t["height"])
        m["by"] = "%s%s" % (info.get("model", "?"), " + consistent" if info.get("method") == "consistent" else "")
        rows.append(m)
    if not rows:
        log("review: nothing upscaled in %s for this selection" % up_dir)
        return 2
    # 'lost' only counts where there was something to lose
    key = (lambda m: m["lost"] if m["contrast"] >= a.min_contrast else -1.0) if a.sort == "lost" else (lambda m: m[a.sort])
    rows.sort(key=lambda m: (-key(m), m["name"].lower()))
    log("review of %d upscaled textures in %s, sorted by '%s' (worst first)" % (len(rows), up_dir, a.sort))
    for k in ("diff", "lost", "added", "patchy"):
        vals = sorted(m[k] for m in rows)
        log("  %-6s median %.2f, 90 %% below %.2f, 99 %% below %.2f, max %.2f" % (k, vals[len(vals) // 2], vals[int(len(vals) * 0.9)], vals[int(len(vals) * 0.99)], vals[-1]))
    log("%-44s %-9s %6s %6s %8s %6s %6s  %s" % ("name", "size", "diff", "lost", "contrast", "added", "patchy", "made by"))
    if a.json:
        write_atomic(os.path.abspath(a.json), json.dumps(rows, indent=1).encode("utf-8"))
    rows = rows[a.skip:a.skip + a.top]
    for m in rows:
        log("%-44s %-9s %6.2f %6.2f %8.2f %6.2f %6.2f  %s" % (m["name"], m["size"], m["diff"], m["lost"], m["contrast"], m["added"], m["patchy"], m["by"]))
    if a.sheet:
        # original (Lanczos) | upscale, a crop from the middle, at the size a x2 pack shows
        cell, cells = 2 * a.crop, []
        per_row = max(1, 1560 // (2 * cell + 12))
        for m in rows:
            t = man["textures"][m["name"]]
            src, up = Image.open(work.src_png(m["name"])), Image.open(up_png(up_dir, t))
            w, h = min(a.crop, t["width"]), min(a.crop, t["height"])
            x, y = (t["width"] - w) // 2, (t["height"] - h) // 2
            pair = Image.new("RGB", (2 * cell + 4, cell + 14), (30, 30, 30))
            for i, im in enumerate((src.crop((x, y, x + w, y + h)).resize((2 * w, 2 * h), Image.LANCZOS),
                                    up.crop((x * SCALE, y * SCALE, (x + w) * SCALE, (y + h) * SCALE)).resize((2 * w, 2 * h), Image.BOX))):
                bg = Image.new("RGBA", im.size, (0, 150, 255, 255))
                bg.alpha_composite(im.convert("RGBA"))
                pair.paste(bg.convert("RGB"), (i * (cell + 4), 14))
            ImageDraw.Draw(pair).text((2, 1), "%s %s d%.1f l%.2f a%.2f p%.2f" % (m["name"], m["size"], m["diff"], m["lost"], m["added"], m["patchy"]), fill=(255, 255, 255))
            cells.append(pair)
        sheet = Image.new("RGB", (per_row * (2 * cell + 12), ((len(cells) + per_row - 1) // per_row) * (cell + 18)), (60, 60, 60))
        for i, c in enumerate(cells):
            sheet.paste(c, ((i % per_row) * (2 * cell + 12), (i // per_row) * (cell + 18)))
        save_png(sheet, os.path.abspath(a.sheet))
        log("sheet (original enlarged | upscale, pairs in the order above): %s" % os.path.abspath(a.sheet))
    return 0


# ---------------------------------------------------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------------------------------------------------
def cmd_verify(a, work):
    low_priority()
    man = work.load()
    root = os.path.abspath(a.dir)
    if not os.path.isdir(root):
        log("no such folder:", root)
        return 2
    by_path = {n.lower(): n for n in man["textures"]}
    files = total = 0
    bad = []
    other_format = [0]
    factors, formats, cats, colours = {}, {}, {}, {}
    t0 = time.time()
    for dp, dn, fn in os.walk(root):
        dn.sort(key=str.lower)
        for f in sorted(fn, key=str.lower):
            path = os.path.join(dp, f)
            rel = os.path.relpath(path, root).replace("/", "\\")
            files += 1
            size = os.path.getsize(path)
            total += size
            try:
                name = by_path.get(rel.lower())
                if name is None:
                    raise ValueError("not a texture name of the manifest")
                if rel != name and a.strict_case:
                    raise ValueError("file name differs from the TTL name '%s' in case" % name)
                t = man["textures"][name]
                with open(path, "rb") as fh:
                    d = fh.read()
                if d[:4] != b"DDS " or len(d) < 128 or U("<I", d, 4)[0] != 124 or U("<I", d, 76)[0] != 32:
                    raise ValueError("not a DDS file")
                info = dds_info(d)
                w, h, fmt = info["width"], info["height"], info["format"]
                # The game keeps the image info of the ORIGINAL on the texture (opaque stays opaque, blended stays
                # blended), so a replacement may have any format that still carries what the original used.
                if t["alpha"]["used"] and fmt not in ("DXT3", "DXT5", "A8R8G8B8"):
                    raise ValueError("format %s has no usable alpha, the original (%s) uses its alpha" % (fmt, t["format"]))
                if fmt != t["out_format"]:
                    if a.strict_format:
                        raise ValueError("format %s, the pack format for an original %s is %s" % (fmt, t["format"], t["out_format"]))
                    other_format[0] += 1
                fx, fy = w / t["width"], h / t["height"]
                if fx != fy or fx not in (0.25, 0.5, 1, 2, 4, 8):
                    raise ValueError("size %dx%d is not a power of two multiple of the original %dx%d" % (w, h, t["width"], t["height"]))
                if a.factor and fx != a.factor:
                    raise ValueError("size factor %g, expected %g" % (fx, a.factor))
                if info["mips"] != len(mip_sizes(w, h)):
                    raise ValueError("%d mip levels, a full chain has %d" % (info["mips"], len(mip_sizes(w, h))))
                if U("<I", d, 8)[0] != 0xA1007 or U("<I", d, 108)[0] != 0x401008:
                    raise ValueError("header flags %#x / caps %#x" % (U("<I", d, 8)[0], U("<I", d, 108)[0]))
                if size != expected_file_bytes(fmt, w, h):
                    raise ValueError("%d bytes, a %s %dx%d with a full chain has %d" % (size, fmt, w, h, expected_file_bytes(fmt, w, h)))
                top = decode_top(d, info)  # proves the top level decodes
                if top.size != (w, h):
                    raise ValueError("decoded size mismatch")
                d_rgb, d_a = split_rgba(top)
                if not t["alpha"]["used"] and d_a is not None and d_a.getextrema()[0] < 255:
                    raise ValueError("the original is opaque, the file has alpha below 255")
                if not a.no_compare:
                    # The picture has to be the original one, upscaled or with the test tint: compare 16x16 thumbnails
                    # (colour averaged with alpha as weight, so the colour of hidden pixels does not count).
                    # Measured on this game: the right picture differs by < 2 (plain) / < 3.4 (tinted), another picture
                    # of the same size by 58 / 25 in the median; about 5 % of swapped pictures still pass.
                    s_rgb, s_a = split_rgba(Image.open(work.src_png(name)))
                    if not t["alpha"]["used"]:
                        s_a = d_a = None
                    elif d_a is None:
                        raise ValueError("the original has alpha, the file has none")
                    st, sa = thumbnail(s_rgb, s_a)
                    dt, da = thumbnail(d_rgb, d_a)
                    tt = tint(st)
                    if sa is not None:
                        if mean_abs_diff(sa, da) > 16.0:
                            raise ValueError("alpha differs from the original (mean difference %.1f)" % mean_abs_diff(sa, da))
                        m = sa.point([0] * 32 + [255] * 224)  # only where something is visible
                        black = Image.new("RGB", st.size)
                        st, tt, dt = Image.composite(st, black, m), Image.composite(tt, black, m), Image.composite(dt, black, m)
                    plain, tinted = mean_abs_diff(st, dt), mean_abs_diff(tt, dt)
                    if plain > 16.0 and tinted > 5.0:
                        raise ValueError("picture differs from the original (mean colour difference %.1f, %.1f as the test tint)" % (plain, tinted))
                    kind = "too little visible to tell" if abs(plain - tinted) < 2.0 else ("magenta test tint" if tinted < plain else "original colours")
                    colours[kind] = colours.get(kind, 0) + 1
                factors[fx] = factors.get(fx, 0) + 1
                formats[fmt] = formats.get(fmt, 0) + 1
                cats[t["category"]] = cats.get(t["category"], 0) + 1
            except Exception as e:
                bad.append((rel, str(e)))
    for rel, why in bad[:60]:
        log("  BAD %s: %s" % (rel, why))
    log("verify %s: %d files, %d bytes (%s), %d bad, %.1f s" % (root, files, total, fmt_bytes(total), len(bad), time.time() - t0))
    log("  size factors: %s | formats: %s | categories: %s" % (dict(sorted(factors.items())), dict(sorted(formats.items())), dict(sorted(cats.items()))))
    if other_format[0]:
        log("  %d files are in another format than their original's class (fine: the game keeps the original's image info; --strict-format refuses them)" % other_format[0])
    if colours:
        log("  colours compared with the originals: %s" % dict(sorted(colours.items())))
    have = {}
    for name, t in man["textures"].items():
        e = have.setdefault(t["category"], [0, 0])
        e[1] += 1
        e[0] += os.path.isfile(out_path(root, name))
    log("  covers: " + ", ".join("%d of %d %s" % (have[c][0], have[c][1], c) for c in ("world", "lut", "hud", "bkg") if c in have))
    return 1 if bad else 0


# ---------------------------------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=DEF_DATA, help="the game's Data folder (only read), default %(default)s")
    ap.add_argument("--work", default=DEF_WORK, help="work folder, default %(default)s")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("extract", help="all unique textures -> src\\*.png + manifest.json")
    sub.add_parser("stats", help="summary of the manifest")

    p = sub.add_parser("upscale", help="4x upscale of the selected textures (resumable)")
    p.add_argument("--exe", help="realesrgan-ncnn-vulkan.exe")
    p.add_argument("--model", help="model name (-n of the exe), e.g. realesrgan-x4plus")
    p.add_argument("--models", help="folder with the model files, default <exe folder>\\models")
    p.add_argument("--fake", action="store_true", help="Lanczos instead of the exe, into <work>\\up_fake (proves the chain)")
    p.add_argument("--up", help="output folder, default <work>\\up (<work>\\up_fake with --fake)")
    p.add_argument("--pad", type=int, default=16, help="pixels added on every side before upscaling, cropped after (default 16)")
    p.add_argument("--wrap", choices=("auto", "always", "never"), default="auto", help="padding: repeat the picture or its edge; auto = the manifest's 'wrap'")
    p.add_argument("--alpha", choices=("bicubic", "model"), default="bicubic", help="how alpha is upscaled: bicubic here (default) or a second pass through the model")
    p.add_argument("--batch", type=int, default=200, help="images per run of the exe (default 200)")
    p.add_argument("--batch-mpx", type=float, default=12.0, help="megapixels of input per run of the exe (default 12)")
    p.add_argument("--tile", type=int, default=0, help="-t of the exe; 0 (default) = every image as one tile, up to --max-tile")
    p.add_argument("--max-tile", type=int, default=704, help="largest tile the GPU takes in one piece (default 704: a padded background picture)")
    p.add_argument("--gpu", type=int, default=None, help="-g of the exe")
    p.add_argument("--jobs", help="-j of the exe, load:proc:save (default: the exe's, 1:1:2 for tiles above 320)")
    p.add_argument("--tta", action="store_true", help="-x of the exe (8x slower)")
    p.add_argument("--no-sanity", action="store_true", help="do not compare the output with the input")
    p.add_argument("--no-overrides", action="store_true", help="ignore <work>\\overrides.json (the model alone for every name)")
    p.add_argument("--force", action="store_true", help="redo what is already there")
    add_select_args(p)

    p = sub.add_parser("override", help="per-name exceptions for upscale (another model, consistent, lanczos)")
    p.add_argument("patterns", nargs="*", metavar="GLOB", help="texture names; none = list the overrides")
    p.add_argument("--method", choices=METHODS, help="model | consistent (model + the original's low frequencies) | lanczos (no model)")
    p.add_argument("--model", help="the model for these names instead of the one of the upscale command")
    p.add_argument("--note", help="why (kept in the file)")
    p.add_argument("--clear", action="store_true", help="remove the overrides of these names")

    p = sub.add_parser("review", help="rank the upscaled textures by how far they left the original")
    p.add_argument("--up", help="the upscaled files, default <work>\\up")
    p.add_argument("--sort", choices=("diff", "lost", "added", "patchy"), default="lost", help="what to rank by (default lost)")
    p.add_argument("--min-contrast", type=float, default=1.5, help="--sort lost: only originals with at least this pixel contrast (default 1.5)")
    p.add_argument("--top", type=int, default=40, help="how many to list / put on the sheet (default 40)")
    p.add_argument("--skip", type=int, default=0, help="start the list / sheet after the first N of the ranking")
    p.add_argument("--sheet", help="write a contact sheet of the listed ones (PNG): original enlarged | upscale")
    p.add_argument("--crop", type=int, default=96, help="--sheet: source pixels shown per texture (default 96)")
    p.add_argument("--json", help="write all numbers to this file")
    add_select_args(p)

    p = sub.add_parser("pack", help="DDS pack from the upscaled files")
    p.add_argument("--out", required=True, help="pack folder (its content goes to Data\\Textures)")
    p.add_argument("--factor", type=int, default=2, help="size relative to the original: 1, 2 or 4 (default 2)")
    p.add_argument("--up", help="the upscaled files, default <work>\\up")
    p.add_argument("--bkg-format", choices=("DXT1", "R8G8B8"), default="DXT1", help="format of the background tiles (default DXT1; the originals are R8G8B8, 6x the bytes)")
    p.add_argument("--bkg-rgb", action="append", default=[], metavar="GLOB", help="background tiles to write as R8G8B8 anyway (repeatable), e.g. \"bkg_Title_*\"")
    p.add_argument("--bkg-rgb-below", type=float, default=0, metavar="DB", help="measure every DXT1 background tile against the uncompressed one and write those below this PSNR as R8G8B8 (0 = off; 1 = only report)")
    p.add_argument("--max-size", type=int, default=0, help="halve textures until their longest side is at most this")
    p.add_argument("--soft-alpha", action="store_true", help="do not turn the alpha of cut-out textures back into 0 / 255")
    p.add_argument("--refine", type=int, default=1, help="least squares passes of the DXT encoder (default 1)")
    p.add_argument("--allow-missing", action="store_true", help="no error for selected textures that were not upscaled")
    p.add_argument("--clean", action="store_true", help="remove files in --out that this run did not write")
    add_select_args(p)

    p = sub.add_parser("testpack", help="the originals with a magenta tint, same size and format")
    p.add_argument("--out", required=True)
    p.add_argument("--clean", action="store_true", help="remove files in --out that this run did not write")
    add_select_args(p)

    p = sub.add_parser("verify", help="re-read a pack and check it against the manifest")
    p.add_argument("dir")
    p.add_argument("--factor", type=float, default=0, help="expect this size factor for every file")
    p.add_argument("--no-compare", action="store_true", help="skip the comparison with the original picture")
    p.add_argument("--strict-case", action="store_true", help="the file names must have the case of the TTL names")
    p.add_argument("--strict-format", action="store_true", help="every file must have the format 'out_format' of the manifest")

    a = ap.parse_args()
    work = Work(a)
    os.makedirs(work.root, exist_ok=True)
    lock = None
    if a.cmd in ("extract", "upscale", "override", "pack", "testpack"):
        # one heavy step at a time per work folder; the lock goes away with the process
        lock = open(os.path.join(work.root, "hd_textures.lock"), "a+b")
        try:
            if os.name == "nt":
                import msvcrt
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            log("another hd_textures.py step is running in %s - wait for it to finish" % work.root)
            return 2
    try:
        return {"extract": cmd_extract, "stats": cmd_stats, "upscale": cmd_upscale, "override": cmd_override, "review": cmd_review,
                "pack": cmd_pack, "testpack": cmd_testpack, "verify": cmd_verify}[a.cmd](a, work)
    finally:
        if lock is not None:
            lock.close()
            try:
                os.remove(os.path.join(work.root, "hd_textures.lock"))
            except OSError:
                pass


if __name__ == "__main__":
    sys.exit(main())
