r"""Rekord26 (Data/GUI/fonts.trb, Fonts\Rekord_Black_26.tga) -> CCThatsAllFolks layout table of the HD fonts
(Toshi/Source/GUI/T2GUIFontSDF_Rekord26.inl, read by T2GUIFontSDF.cpp).

The retail Rekord26 atlas was made from CCThatsAllFolks: every atlas glyph is a TTF glyph at 33.7 px per em with a
0.4 px bolder fill. For every character of the game font this finds the TTF glyph(s) and where they go, measured on
the HD atlas: the fill (white) of each atlas glyph against the TTF outline.
- Plain characters: the TTF glyph that matches best (the same character or its other case, the atlas mixes them),
  uniform scale, centred on the atlas fill. A few punctuation glyphs the atlas draws bigger or smaller are fitted into
  its fill; & (the atlas draws O umlaut there, a retail bug) is the TTF ampersand on the baseline.
- Accented letters: the TTF base letter fitted into the atlas letter (the accented capitals of the comic font are
  squashed), the TTF accent (acute, grave, circumflex, tilde, dieresis, ring) fitted into the atlas accent; o and u
  with double acute: two narrow TTF acutes over the dots of o/u umlaut.
- Characters whose base or accent the TTF hasn't got (C cedilla, AE, O slash, sharp s, OE...) stay on the atlas.

Inputs: fonts.json and the HD atlas PNG written by DevTools/HDFonts/font_tool.py (extract), the TTF the user supplied.
Usage: py -B gen_layout.py <fonts.json> <Rekord_Black_26_hd.png> <CCThatsAllFolks.ttf> <out.inl> <preview.png>
The preview shows every atlas glyph next to the TTF fill (red) placed over it.
"""
import json
import sys
import unicodedata

import numpy as np
from PIL import Image
from scipy import ndimage
from fontTools.ttLib import TTFont
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import RecordingPen

fonts_json, atlas_png, ttf_path, out_inl, preview_png = sys.argv[1:6]
d = json.load(open(fonts_json, encoding='utf-8'))
F = [f for f in d['fonts'] if f['name'] == 'Rekord26'][0]
S = 2  # HD atlas texels per font pixel
atlas = np.array(Image.open(atlas_png).convert('RGBA')).astype(float) / 255
tt = TTFont(ttf_path)
gs = tt.getGlyphSet()
cmap = tt.getBestCmap()
K = 0.03373      # font px per TTF unit, fitted on the plain letters
BLEED = 0.397    # the atlas fill is this much bolder than the outline, font px
BASELINE = 20.29 # font px below the top of the line (a_fY of DrawTextSingleLine)
# Glyphs of the same design the atlas draws bigger or smaller than the TTF (IoU of the plain fit below 0.75)
RESIZE = {0x22, 0x23, 0x24, 0x27, 0x29, 0x3B, 0x3D, 0x5F}

glyphs = {int(k, 16): dict(g) for k, g in F['glyphs'].items()}
# The retail map fixes T2GUIFont applies (s_aRekord26Aliases): N tilde .. O umlaut take the lower case glyphs
for up, lo in [(0xD1, 0xF1), (0xD2, 0xF2), (0xD3, 0xF3), (0xD4, 0xF4), (0xD5, 0xF5), (0xD6, 0xF6)]:
    glyphs[up] = dict(glyphs[lo])
# The extra glyphs take the data of their base glyphs (s_aRekord26ExtraGlyphs)
for c, base in [(0x150, 0xF6), (0x151, 0xF6), (0x170, 0xDC), (0x171, 0xFC)]:
    glyphs[c] = dict(glyphs[base])


def ttf_bounds(cp):
    gn = cmap.get(cp)
    if gn is None:
        return None
    bp = BoundsPen(gs)
    gs[gn].draw(bp)
    return bp.bounds


def cell_fill(g):
    x0, y0, w, h = g['x'] * S, g['y'] * S, (g['w'] + 1) * S, (g['h'] + 1) * S
    c = np.zeros((h, w, 4))
    part = atlas[y0:y0 + h, x0:x0 + w]
    c[:part.shape[0], :part.shape[1]] = part
    return c, (c[..., 0] > 0.5) & (c[..., 3] > 0.5)


def components(fill):
    lab, n = ndimage.label(fill)
    out = []
    for i in range(1, n + 1):
        ys, xs = np.nonzero(lab == i)
        if len(ys) < 6:
            continue
        out.append(dict(x0=xs.min() / S, x1=(xs.max() + 1) / S, y0=ys.min() / S, y1=(ys.max() + 1) / S, n=len(ys), mask=(lab == i)))
    return out


_poly_cache = {}


def contours(cp):
    if cp in _poly_cache:
        return _poly_cache[cp]
    rp = RecordingPen()
    gs[cmap[cp]].draw(rp)
    polys = []
    cur = []
    last = None
    for op, args in rp.value:
        if op == 'moveTo':
            cur = [args[0]]
            last = args[0]
        elif op == 'lineTo':
            cur.append(args[0])
            last = args[0]
        elif op == 'qCurveTo':
            pts = list(args)
            start = last
            for i in range(len(pts) - 1):
                c1 = pts[i]
                end = pts[i + 1] if i == len(pts) - 2 else ((pts[i][0] + pts[i + 1][0]) / 2, (pts[i][1] + pts[i + 1][1]) / 2)
                for t in np.linspace(0, 1, 9)[1:]:
                    it = 1 - t
                    cur.append((it * it * start[0] + 2 * it * t * c1[0] + t * t * end[0], it * it * start[1] + 2 * it * t * c1[1] + t * t * end[1]))
                start = end
            last = pts[-1]
        elif op == 'curveTo':
            raise RuntimeError('cubic')
        elif op in ('closePath', 'endPath'):
            if len(cur) > 2:
                polys.append(np.array(cur, float))
            cur = []
    _poly_cache[cp] = polys
    return polys


def render(pieces, w, h, scale=S * 4):
    """Coverage of the pieces (cp, sx, sy, ox, oy: font px per unit and origin in font px, y down) at S px per font px."""
    W, H = int(w * scale), int(h * scale)
    acc = np.zeros((H, W), bool)
    yy, xx = np.mgrid[0:H, 0:W]
    px = xx + 0.5
    py = yy + 0.5
    for (cp, sx, sy, ox, oy) in pieces:
        wind = np.zeros((H, W), np.int32)
        for P in contours(cp):
            X = (ox + P[:, 0] * sx) * scale
            Y = (oy - P[:, 1] * sy) * scale
            X1 = np.roll(X, -1)
            Y1 = np.roll(Y, -1)
            for a, b, c, e in zip(X, Y, X1, Y1):
                if b == e:
                    continue
                lo, hi = (b, e) if b < e else (e, b)
                r0 = max(int(np.ceil(lo - 0.5)), 0)
                r1 = min(int(np.ceil(hi - 0.5)), H)
                if r1 <= r0:
                    continue
                rows = py[r0:r1, :]
                xi = a + (rows - b) / (e - b) * (c - a)
                hit = px[r0:r1, :] < xi
                wind[r0:r1, :] += np.where(hit, 1 if e > b else -1, 0)
        acc |= wind != 0
    f = scale // S
    return acc.reshape(H // f, f, W // f, f).mean(axis=(1, 3))


def fit_plain(cp_ttf, box):
    b = ttf_bounds(cp_ttf)
    cx = (b[0] + b[2]) / 2
    cy = (b[1] + b[3]) / 2
    ox = (box['x0'] + box['x1']) / 2 - cx * K
    oy = (box['y0'] + box['y1']) / 2 + cy * K
    return (cp_ttf, K, K, ox, oy)


def fit_box(cp_ttf, box, uniform=False):
    b = ttf_bounds(cp_ttf)
    bw, bh = b[2] - b[0], b[3] - b[1]
    sx = max(box['x1'] - box['x0'] - 2 * BLEED, 0.5) / bw
    sy = max(box['y1'] - box['y0'] - 2 * BLEED, 0.5) / bh
    if uniform:
        sx = sy = (sx + sy) / 2
    cx = (b[0] + b[2]) / 2
    cy = (b[1] + b[3]) / 2
    ox = (box['x0'] + box['x1']) / 2 - cx * sx
    oy = (box['y0'] + box['y1']) / 2 + cy * sy
    return (cp_ttf, sx, sy, ox, oy)


def iou(a, b):
    a = a > 0.5
    b = b > 0.5
    u = (a | b).sum()
    return (a & b).sum() / u if u else 0


def other_case(cp):
    s = chr(cp).swapcase()
    return ord(s) if len(s) == 1 else cp


def decompose(cp):
    dec = unicodedata.decomposition(chr(cp)).split()
    if len(dec) != 2 or dec[0].startswith('<'):
        return None
    base, mark = int(dec[0], 16), int(dec[1], 16)
    m = {0x0301: 0x00B4, 0x0300: 0x0060, 0x0302: 0x005E, 0x0303: 0x007E, 0x0308: 0x00A8, 0x030A: 0x00B0, 0x030B: 0x02DD}.get(mark)
    return (base, m) if m else None


entries = []
report = []
for cp in sorted(glyphs):
    g = glyphs[cp]
    if cp in (0x20, 0xA0) or 0xF00 <= cp <= 0xFFF or g['w'] == 0 or g['h'] == 0:
        continue
    c, fill = cell_fill(g)
    comps = components(fill)
    if not comps:
        continue
    W, H = g['w'] + 1, g['h'] + 1
    dec = decompose(cp)
    if cp in (0x150, 0x151, 0x170, 0x171):
        dec = ({0x150: 0x4F, 0x151: 0x6F, 0x170: 0x55, 0x171: 0x75}[cp], 0x02DD)
    if dec:
        base, mark = dec
        if base not in cmap and other_case(base) not in cmap:
            report.append('%04X %s: base not in the TTF, atlas' % (cp, chr(cp)))
            continue
        if mark != 0x02DD and mark not in cmap:
            report.append('%04X %s: accent U+%04X not in the TTF, atlas' % (cp, chr(cp), mark))
            continue
        comps.sort(key=lambda q: -q['n'])
        body = comps[0]
        acc = comps[1:]
        if not acc:
            report.append('%04X %s: no accent part in the atlas glyph, atlas' % (cp, chr(cp)))
            continue
        abox = dict(x0=min(q['x0'] for q in acc), x1=max(q['x1'] for q in acc), y0=min(q['y0'] for q in acc), y1=max(q['y1'] for q in acc))
        best = None
        for bc in sorted({base, other_case(base)}):
            if bc not in cmap:
                continue
            pb = fit_box(bc, body)
            sc = iou(render([pb], W, H), body['mask'])
            if not best or sc > best[0]:
                best = (sc, pb)
        sc_body, pb = best
        pieces = [pb]
        if mark == 0x02DD:
            # Two acutes, narrower and steeper than one fitted to the dots, one over each dot of the umlaut
            for q in sorted(acc, key=lambda q: q['x0'])[:2]:
                cxq = (q['x0'] + q['x1']) / 2
                pieces.append(fit_box(0xB4, dict(x0=cxq - 3.4, x1=cxq + 3.4, y0=q['y0'] - 2.6, y1=q['y1'] + 0.4)))
        else:
            pieces.append(fit_box(mark, abox, uniform=(mark == 0xB0)))
        sc = iou(render(pieces, W, H), fill) if mark != 0x02DD else sc_body
        entries.append((cp, g, pieces, 'body U+%04X IoU %.2f, all %.2f' % (pb[0], sc_body, sc)))
        continue
    if cp not in cmap and other_case(cp) not in cmap:
        report.append('%04X %s: not in the TTF, atlas' % (cp, chr(cp)))
        continue
    box = dict(x0=min(q['x0'] for q in comps), x1=max(q['x1'] for q in comps), y0=min(q['y0'] for q in comps), y1=max(q['y1'] for q in comps))
    best = None
    for cand in sorted({cp, other_case(cp)}):
        if cand not in cmap:
            continue
        p = fit_plain(cand, box)
        sc = iou(render([p], W, H), fill)
        if not best or sc > best[0]:
            best = (sc, p)
    sc, p = best
    if sc < 0.75 and cp in RESIZE:
        # The same glyph drawn a little bigger or smaller in the atlas: the TTF glyph fitted into the atlas fill
        part = max(comps, key=lambda q: q['n']) if cp == 0x29 else box
        p = fit_box(cp, part)
        sc = iou(render([p], W, H), fill)
        entries.append((cp, g, [p], 'U+%04X fitted, IoU %.2f' % (p[0], sc)))
        continue
    if sc < 0.75 and cp == 0x26:
        # Retail bug: & points at the O umlaut glyph. The TTF ampersand on the baseline, centred in that cell
        b = ttf_bounds(cp)
        p = (cp, K, K, (box['x0'] + box['x1']) / 2 - (b[0] + b[2]) / 2 * K, BASELINE - (F['line_height'] + g['yoff'] - g['h']))
        entries.append((cp, g, [p], 'U+0026 on the baseline (the atlas draws O umlaut)'))
        continue
    if sc < 0.75:
        report.append('%04X %s: best TTF match U+%04X IoU %.2f, atlas' % (cp, chr(cp), p[0], sc))
        continue
    entries.append((cp, g, [p], 'U+%04X IoU %.2f' % (p[0], sc)))

for line in report:
    print(line)
print(len(entries), 'SDF glyphs')

# Preview: atlas glyph | TTF fill (red) over it, 3x
from PIL import ImageDraw
Z = 3
cols = 10
pw, ph = 2 * 34 * Z + 12, 52 * Z + 16
rows_n = (len(entries) + cols - 1) // cols
sheet = Image.new('RGB', (cols * pw, rows_n * ph), (40, 40, 40))
dr = ImageDraw.Draw(sheet)
for i, (cp, g, pieces, note) in enumerate(entries):
    c, fill = cell_fill(g)
    W, H = g['w'] + 1, g['h'] + 1
    a = c[..., 3:4]
    comp = c[..., :3] * a + 0.45 * (1 - a)
    m = render(pieces, W, H)
    over = comp.copy()
    over[..., 0] = np.clip(comp[..., 0] * (1 - m) + m, 0, 1)
    over[..., 1] = comp[..., 1] * (1 - 0.6 * m)
    over[..., 2] = comp[..., 2] * (1 - 0.6 * m)
    im1 = Image.fromarray((comp * 255).astype(np.uint8)).resize((W * Z * S // 2, H * Z * S // 2), Image.NEAREST)
    im2 = Image.fromarray((over * 255).astype(np.uint8)).resize((W * Z * S // 2, H * Z * S // 2), Image.NEAREST)
    x = (i % cols) * pw
    y = (i // cols) * ph
    sheet.paste(im1, (x, y + 14))
    sheet.paste(im2, (x + 34 * Z + 4, y + 14))
    dr.text((x + 2, y + 1), '%s %04X %s' % (chr(cp), cp, note.split(',')[0][:22]), fill=(255, 220, 120))
sheet.save(preview_png)

with open(out_inl, 'w', newline='\r\n', encoding='utf-8') as o:
    o.write('// Generated by DevTools/HDFonts/gen_layout.py from the retail Rekord26 HD atlas and CCThatsAllFolks-Regular.ttf\n')
    o.write('// { character, { cell width, cell height, left padding, y offset }, { { TTF character, scale x, scale y, origin x, origin y }, ... } }\n')
    for cp, g, pieces, note in entries:
        ps = ', '.join('{ 0x%04X, %.5ff, %.5ff, %.3ff, %.3ff }' % (p[0], p[1], p[2], p[3], p[4]) for p in pieces)
        o.write('{ 0x%04X, { %d, %d, %d, %d }, %d, { %s } }, // %s\n' % (cp, g['w'], g['h'], g['left'], g['yoff'], len(pieces), ps, note))
