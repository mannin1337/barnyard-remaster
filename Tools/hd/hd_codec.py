"""Barnyard material library reader, image processing and DDS mip encoder."""
import io
import struct
from PIL import Image, ImageChops, ImageFilter, ImageMath

SCALE = 4
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


