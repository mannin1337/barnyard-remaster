#!/usr/bin/env python3
"""Build the v5.5 loose texture pack from a local Barnyard PC installation."""
import argparse
from collections import Counter, defaultdict
import ctypes
from dataclasses import dataclass
from functools import lru_cache
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

# Keep the CPU available while the user plays or works.
for variable in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "VECLIB_MAXIMUM_THREADS"):
    os.environ[variable] = "1"

from PIL import Image
import hd_codec as hd
import nature

HERE = Path(__file__).resolve().parent
TERRAIN = {name: kind for kind, names in nature.groups.items() for name in names.split()}
MODEL = "realesrgan-x4plus"
MODEL_HASHES = {
    ".param": "35330ececcea33b6c397a72548e788d5d53becee4734c50b7fada36e89f10a86",
    ".bin": "713ee713b0353afaa27976f0563a64a5043bd70b9bd8936c2e26e25ebcdbcddf",
}


@dataclass(frozen=True)
class Texture:
    name: str
    library: str
    md5: str
    sha256: str
    format: str
    width: int
    height: int
    flags: int
    picture: str
    method: str
    tile: int
    effect: str

    @property
    def wrap(self):
        return (bool(self.flags & 4), bool(self.flags & 8))


def texture_catalog():
    raw = json.loads((HERE / "textures.json").read_text(encoding="utf-8"))
    return [Texture(*row) for row in raw["textures"]]


def low_priority():
    if os.name == "nt":
        kernel = ctypes.windll.kernel32
        kernel.GetCurrentProcess.restype = ctypes.c_void_p
        kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x4000)
    elif hasattr(os, "nice"):
        os.nice(8)


def checked_child(root, relative):
    path = (root / relative).resolve()
    if path == root or not path.is_relative_to(root):
        raise ValueError(f"Path escapes its directory: {relative}")
    return path


class Sources:
    def __init__(self, game):
        self.game = game

    @lru_cache(maxsize=8192)
    def path(self, relative):
        direct = checked_child(self.game, "Data/" + relative)
        if direct.is_file():
            return direct
        # Original CD case can differ across installers. Resolve on Linux too.
        path = self.game
        for part in ("Data", *Path(relative).parts):
            matches = [p for p in path.iterdir() if p.name.casefold() == part.casefold()]
            if len(matches) != 1:
                raise FileNotFoundError(f"Missing or ambiguous original: Data/{relative}")
            path = matches[0]
        return checked_child(self.game, path.relative_to(self.game))

    @lru_cache(maxsize=2)
    def library(self, name):
        filename, _, embedded = name.partition("::")
        doc = self.document(filename)
        for symbol, section, offset in doc.symbols:
            if symbol.casefold() == (embedded or "TTL").casefold():
                # Some libraries have repeated names. Retain the checked source
                # rather than depending on its table position.
                return {(n.replace("\\", "/").lower(), hashlib.md5(data).hexdigest()): data
                        for n, data in hd.read_ttl(doc, section, offset)}
        raise ValueError(f"No texture library: {name}")

    @lru_cache(maxsize=1)
    def document(self, filename):
        return hd.TRB(self.path(filename).read_bytes())

    def image(self, tex):
        binary = self.library(tex.library).get((tex.name.lower(), tex.md5))
        if binary is None:
            raise ValueError(f"Unrecognised original texture {tex.name} in {tex.library}")
        image = hd.decode_top(binary, hd.dds_info(binary))
        return image if tex.flags & 1 else image.convert("RGB")


def prepared(sources, group):
    originals = [sources.image(tex) for tex in group]
    if group[0].picture:
        rgb, alpha, wrap = hd.assemble_picture(originals), None, (False, False)
    else:
        rgb, alpha = hd.split_rgba(originals[0])
        wrap = group[0].wrap
    rgb = hd.pad_image(rgb, 16, wrap)
    if alpha is not None:
        alpha = hd.pad_image(alpha, 16, wrap)
        rgb = hd.bleed(rgb, alpha)
    return originals, rgb, alpha


def finish(tex, enlarged, original, sources):
    rgb, alpha = hd.split_rgba(enlarged)
    size = (tex.width, tex.height)
    rgb = rgb.resize(size, Image.Resampling.BOX)
    alpha = alpha.resize(size, Image.Resampling.BOX) if tex.flags & 1 else None
    if tex.flags & 2:
        alpha = alpha.point([0] * 128 + [255] * 128)
    data = hd.build_dds(rgb, alpha, tex.format)
    if tex.effect == "otis":
        return nature.otis(data, sources.path)
    if tex.effect == "nature":
        name = Path(tex.name).stem.lower()
        if name in TERRAIN:
            return nature.terrain(name, TERRAIN[name], data, original, tex.wrap)
        return nature.foliage(tex.name, data, original, tex.wrap)
    return data


def model_profile(executable):
    if not executable:
        return "lanczos"
    if not executable.is_file():
        raise FileNotFoundError(executable)
    for ext, expected in MODEL_HASHES.items():
        path = executable.parent / "models" / (MODEL + ext)
        if sha_file(path) != expected:
            raise ValueError(f"Expected the original {MODEL} weight/configuration: {path}")
    return "x4plus-" + sha_file(executable)


def sha_file(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def atomic_save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        tmp = Path(stream.name)
        try:
            stream.write(data)
        except BaseException:
            stream.close()
            tmp.unlink(missing_ok=True)
            raise
    try:
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def batches(groups, gpu):
    buckets = defaultdict(list)
    for group in groups:
        tile = group[0].tile if gpu and group[0].method != "lanczos" else None
        buckets[tile].append(group)
    for tile, items in buckets.items():
        items.sort(key=lambda group: group[0].library)
        n = 1 if tile is None or tile > 320 else 8
        for i in range(0, len(items), n):
            yield tile, items[i:i+n]


def render_batch(sources, batch, tile, executable):
    with tempfile.TemporaryDirectory(prefix="barnyard-hd-") as root:
        root = Path(root)
        inputs, outputs = root / "input", root / "output"
        inputs.mkdir()
        outputs.mkdir()
        samples = [prepared(sources, group) for group in batch]
        if tile is not None:
            for i, (_, rgb, _) in enumerate(samples):
                rgb.save(inputs / f"{i:02}.png", compress_level=1)
            command = [str(executable), "-i", str(inputs), "-o", str(outputs), "-n", MODEL,
                       "-m", str(executable.parent / "models"), "-s", "4", "-f", "png", "-j", "1:1:1"]
            if tile:
                command += ["-t", str(tile)]
            flags = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS if os.name == "nt" else 0
            log = root / "upscaler.log"
            with log.open("wb") as stream:
                result = subprocess.run(command, cwd=executable.parent, creationflags=flags,
                                        stdout=stream, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(f"Upscaler exited with {result.returncode}: " +
                                   log.read_text(encoding="utf-8", errors="replace")[-2000:])
        for i, (group, (originals, rgb, alpha)) in enumerate(zip(batch, samples)):
            full = (rgb.width * 4, rgb.height * 4)
            if tile is None:
                up = hd.lanczos_up(rgb)
            else:
                with Image.open(outputs / f"{i:02}.png") as img:
                    up = img.convert("RGB")
                if up.size != full:
                    raise ValueError("Unexpected upscaler output size")
                if group[0].method == "consistent":
                    up = hd.back_project(up, rgb)
            crop = (64, 64, full[0]-64, full[1]-64)
            up = up.crop(crop)
            alpha = alpha.resize(full, Image.Resampling.BICUBIC).crop(crop) if alpha is not None else None
            images = hd.cut_picture(up, originals) if group[0].picture else [hd.merge_rgba(up, alpha)]
            yield from zip(group, originals, images)


def build(args):
    game, output = args.game.resolve(), args.output.resolve()
    if not game.is_dir() or game.is_relative_to(output) or output.is_relative_to(game):
        raise ValueError("--game and --output must be separate directories")
    executable = args.upscaler.resolve() if args.upscaler else None
    profile = model_profile(executable)
    recipe = hashlib.sha256()
    for file in ("build_hd.py", "hd_codec.py", "nature.py", "textures.json"):
        recipe.update((HERE / file).read_bytes())
    signature = f"{profile}/{recipe.hexdigest()}/{Image.__version__}/{nature.np.__version__}"
    sources = Sources(game)
    catalog = texture_catalog()
    selected = {t.name for t in catalog if not args.only or
                any(fnmatch.fnmatchcase(t.name.lower(), pattern.replace("\\", "/").lower()) for pattern in args.only)}
    if not selected:
        raise ValueError("No texture matched --only")
    ledger_path = checked_child(output, ".barnyard-hd.json")
    ledger = json.loads(ledger_path.read_text(encoding="utf8")) if ledger_path.is_file() else {}
    groups = defaultdict(list)
    pending, counts = {}, Counter()
    for tex in catalog:
        groups[tex.picture or tex.name].append(tex)
        if tex.name not in selected:
            continue
        dest = checked_child(output, "Data/Textures/" + tex.name)
        existing = sha_file(dest) if dest.is_file() else None
        record = ledger.get(tex.name, {})
        if existing == tex.sha256:
            counts["release_match"] += 1
        elif existing and record.get("sha256") == existing and record.get("profile") == signature:
            counts["local_variant"] += 1
        else:
            pending[tex.name] = (dest, existing)
    groups = [sorted(group, key=lambda t: t.name) for group in groups.values() if any(t.name in pending for t in group)]
    print(f"{len(selected)} selected, {len(pending)} to build. Profile: {profile}", flush=True)
    for tile, batch in batches(groups, bool(executable)):
        dirty = False
        for tex, original, enlarged in render_batch(sources, batch, tile, executable):
            if tex.name not in pending:
                continue
            dest, existing = pending[tex.name]
            data = finish(tex, enlarged, original, sources)
            digest = hashlib.sha256(data).hexdigest()
            current = sha_file(dest) if dest.is_file() else None
            if current != existing and current != digest:
                raise ValueError(f"Texture changed while the build was running: {dest}")
            if existing != digest:
                if existing and existing != ledger.get(tex.name, {}).get("sha256"):
                    raise ValueError(f"Preserved an existing, unrecognised texture: {dest}")
                atomic_save(dest, data)
            ledger[tex.name] = {"profile": signature, "sha256": digest}
            dirty = True
            counts["release_match" if digest == tex.sha256 else "local_variant"] += 1
        if dirty:
            atomic_save(ledger_path, json.dumps(ledger, sort_keys=True).encode())
        if sum(counts.values()) % 50 < len(batch):
            print(f"Processed {sum(counts.values())} / {len(selected)}", flush=True)
    print(f"Done. Release SHA-256 matches: {counts['release_match']}. Local variants: {counts['local_variant']}.")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game", type=Path, required=True, help="Original CD installation, containing Data")
    ap.add_argument("--output", type=Path, required=True, help="Separate remaster installation (install.py output)")
    ap.add_argument("--upscaler", type=Path, help="Real-ESRGAN NCNN Vulkan executable, with models/ beside it")
    ap.add_argument("--only", action="append", metavar="GLOB", help="Build a subset, e.g. World/grassmain.tga")
    args = ap.parse_args()
    low_priority()
    try:
        build(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(f"HD build: {error}")


if __name__ == "__main__":
    main()
