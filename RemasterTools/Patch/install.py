#!/usr/bin/env python3
"""Install the Barnyard remaster from the player's original Windows PC discs."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True
import locale_binary as locale

HERE = Path(__file__).resolve().parent
CHUNK = 1024 * 1024


def json_file(path):
    return json.loads(path.read_text(encoding="utf-8"))


class Source:
    """Resolve the same disc filenames on Windows and case-sensitive Linux filesystems."""
    def __init__(self, root):
        self.root = root
        self.dirs = {}

    def file(self, relative):
        current = self.root
        for component in relative.split("/"):
            exact = current / component
            if exact.exists():
                current = exact
                continue
            if current not in self.dirs:
                names = {}
                if current.is_dir():
                    for member in current.iterdir():
                        key = member.name.casefold()
                        names[key] = member if key not in names else None
                self.dirs[current] = names
            current = self.dirs[current].get(component.casefold())
            if current is None:
                return None
        return current if current.is_file() else None


def produce(src, target, expected):
    if not src or not src.is_file() or src.stat().st_size != expected[0]:
        raise ValueError("Missing file or different edition: " + str(src or "(not found)"))
    if target:
        target.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    with src.open("rb") as reader:
        writer = target.open("xb") if target else None
        try:
            for chunk in iter(lambda: reader.read(CHUNK), b""):
                digest.update(chunk)
                if writer:
                    writer.write(chunk)
        finally:
            if writer:
                writer.close()
    if digest.hexdigest() != expected[1]:
        raise ValueError("SHA-256 mismatch: " + str(src))


def checked_result(path, data, expected, stage):
    if len(data) != expected[0] or hashlib.sha256(data).hexdigest() != expected[1]:
        raise ValueError("Generated file differs from the release: " + path)
    if stage:
        dest = stage / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        with dest.open("xb") as writer:
            writer.write(data)


def languages(game, recipe):
    base = locale.parse_locale(locale.TRB(game.file("Data/Locale/eng.trb").read_bytes()))
    for i, text in recipe["locale"].items():
        base[int(i)] = text
    yield "Data/Locale/hun.trb", locale.build_locale(base)
    translated = {"locale:%04d" % i: text for i, text in enumerate(base)}
    for stem, changes in recipe["subs"].items():
        raw = game.file("Data/Subs/ENG/" + stem + ".trb").read_bytes()
        version, original = locale.parse_subs(locale.TRB(raw))
        updated = []
        for name, lines in original:
            values = []
            for i, (start, end, original_text) in enumerate(lines):
                key = "subs:%s:%s:%d" % (stem, name, i)
                translated[key] = changes.get(key, original_text)
                values.append((start, end, translated[key]))
            updated.append((name, values))
        yield "Data/Subs/HUN/" + stem + ".trb", locale.build_subs(version, updated)
    for lang, entries in recipe["extra_subs"].items():
        yield "Data/Subs/" + lang + "/lib_ss_extra.trb", locale.build_subs(0x100, entries)
    mod = {row[0]: (translated[row[0]] if len(row) == 1 else row[1]) for row in recipe["mod"]}
    yield "Data/Locale/hun_mod.json", (json.dumps(mod, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def install(args, stage):
    game = Source(args.game)
    catalogue = json_file(HERE / "files.json")
    release = json_file(HERE / "release.json")
    for key, expected in release["inputs"].items():
        produce(HERE / key, None, expected)
    print("Checking 5,393 retail data files and both original runtime libraries...", flush=True)
    for path, expected in catalogue["retail"].items():
        src = game.file(path)
        if not src:
            raise ValueError("Original PC file missing: " + path)
        dest = path if path.startswith("Data/") else "OpenBarnyard/" + path
        delta = catalogue["delta"].get(path)
        if delta:
            produce(src, None, expected)
            base = src.read_bytes()
            diff = zlib.decompress(base64.b85decode(delta[2]))
            data = bytes(a ^ b for a, b in zip(base, diff)) + diff[len(base):]
            checked_result(dest, data, delta, stage)
        else:
            produce(src, stage / dest if stage else None, expected)
    for path, data in languages(game, json_file(HERE / "translations.json")):
        checked_result(path, data, catalogue["language"][path], stage)
    for path, expected in release["program"].items():
        produce(HERE / "program" / path, stage / path if stage else None, expected)
    hd = saved = 0
    if args.previous:
        old = Source(args.previous)
        if not old.file("OpenBarnyard/OpenBarnyard.exe") or not old.file("Data/Locale/hun.trb"):
            raise ValueError("--previous must point to the previous remaster's root (the folder containing Data).")
        for path, expected in catalogue["legacy_hd"].items():
            src = old.file(path)
            if src:
                produce(src, stage / path if stage else None, expected)
                hd += 1
        if args.copy_saves:
            save = old.root / "OpenBarnyard" / "Save"
            if not save.is_dir() or save.is_symlink():
                raise ValueError("Previous OpenBarnyard/Save folder not found.")
            for src in save.rglob("*"):
                if src.is_symlink() or save.resolve() not in src.resolve().parents:
                    raise ValueError("Unexpected link in the save folder: " + str(src))
                if src.is_file() and stage:
                    dst = stage / "OpenBarnyard" / "Save" / src.relative_to(save)
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(src, dst)
                saved += int(src.is_file())
    if stage:
        (stage / "OpenBarnyard" / "Save").mkdir(exist_ok=True)
        (stage / "Logs").mkdir(exist_ok=True)
    print("Verified 4 data patches, 14 language files, program and shader cache. "
          "Previous HD files: %d. Saves selected: %d." % (hd, saved), flush=True)


def overlaps(a, b):
    return a == b or a in b.parents or b in a.parents


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--game", type=Path, required=True, help="Installed original PC game with Data and the two original DLLs")
    ap.add_argument("--output", type=Path, required=True, help="Separate new or empty remaster directory")
    ap.add_argument("--previous", type=Path, help="Optional previously installed v5.5 remaster with its HD textures")
    ap.add_argument("--copy-saves", action="store_true", help="Copy your own save files from --previous")
    ap.add_argument("--verify-only", action="store_true", help="Verify all required input and resulting patch/locale hashes without writing")
    args = ap.parse_args()
    args.game = args.game.expanduser().resolve()
    args.output = args.output.expanduser().resolve()
    if args.previous:
        args.previous = args.previous.expanduser().resolve()
    if args.copy_saves and not args.previous:
        ap.error("--copy-saves requires --previous")
    if not args.game.is_dir() or (args.previous and not args.previous.is_dir()):
        ap.error("Game root or previous remaster was not found")
    if overlaps(args.game, args.output) or overlaps(HERE, args.output) or (args.previous and overlaps(args.previous, args.output)):
        ap.error("Choose a separate output directory outside the game, old remaster and installer")
    if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
        ap.error("The output directory must be new or empty")
    if args.verify_only:
        install(args, None)
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".barnyard-install-", dir=str(args.output.parent)))
    try:
        install(args, stage)
        if args.output.exists():
            args.output.rmdir()  # Only an empty directory is accepted, including at commit time.
        stage.rename(args.output)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    print("Installed:", args.output)
    print("Windows launchers: Play_Barnyard.bat, Host_LAN.bat, Join_LAN.bat")
    print("Executable:", args.output / "OpenBarnyard" / "OpenBarnyard.exe")
    print("Working directory:", args.output)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as exc:
        sys.exit("Installation failed: " + str(exc))
