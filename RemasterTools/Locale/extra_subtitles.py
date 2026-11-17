"""Build an SS subtitle overlay from reviewed bilingual cues."""

import argparse
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import locale_tool as locale


ROOT = Path(__file__).resolve().parent.parent
LIBRARY = "lib_ss_extra"
VOICE_BANK = "data/sound/fsb/ss.fsb"


def with_subs(data):
    trb = locale.TRB(data)
    source = locale.pb_leaves(trb)
    if ("Main/VOStream", "String", VOICE_BANK) not in [
        (path, kind, value.lower() if isinstance(value, str) else value)
        for path, kind, value in source
    ]:
        raise ValueError("The scene does not reference the SS sound bank.")
    if any(path.lower() == "main/subs" for path, _, _ in source):
        raise ValueError("The scene already has a subtitle library.")
    if trb.sym("Main") != (0, 0) or len(trb.sections) != 1 or trb.secinfo[0][:2] != (0, 0):
        raise ValueError("Unrecognized scene TRB layout.")

    section = bytearray(trb.sections[0])
    old_array = trb.ptr(0, 4)
    count = locale.u32(section, 8)
    if not old_array or old_array[0] != 0 or count > 1000:
        raise ValueError("Invalid Main property array.")
    old_start = old_array[1]
    old_end = old_start + 8 * count
    if old_end > len(section):
        raise ValueError("The Main property array exceeds its section.")
    relocs = list(trb.relocs)

    def append(payload):
        section.extend(b"\0" * (-len(section) % 4))
        offset = len(section)
        section.extend(payload)
        return offset

    property_text = append(b"Subs\0")
    library_text = append(LIBRARY.encode("ascii") + b"\0")
    name = append(struct.pack("<I", property_text))
    value = append(struct.pack("<II", locale.PB_TYPES.index("String"), library_text))
    new_array = append(section[old_start:old_end] + struct.pack("<II", name, value))
    for pointer in range(old_start, old_end, 4):
        target = trb.rel.get((0, pointer))
        if target is not None:
            relocs.append((0, target, new_array + pointer - old_start))
    relocs.extend((0, 0, offset) for offset in (name, value + 4, new_array + 8 * count, new_array + 8 * count + 4))
    struct.pack_into("<II", section, 4, new_array, count + 1)
    section.extend(b"\0" * (-len(section) % 4))
    symbols = [(symbol["sec"], symbol["name"], symbol["off"]) for symbol in trb.symbols]
    output = locale.build_trb([bytes(section)], relocs, symbols, trb.version)
    expected = source + [("Main/Subs", "String", LIBRARY)]
    if locale.pb_leaves(locale.TRB(output)) != expected:
        raise ValueError("Scene properties changed when the generated TRB was read back.")
    return output


def write_output(output, files):
    output.mkdir(parents=True, exist_ok=False)
    for relative, contents in files.items():
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(contents)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True, help="Path to the original PC game Data directory")
    parser.add_argument("--transcript", type=Path, default=Path(__file__).with_name("extra_tracks.json"))
    parser.add_argument("--review", type=Path, default=Path(__file__).with_name("extra_cues.json"))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = args.data.resolve()
    transcript = json.loads(args.transcript.read_text(encoding="utf-8"))
    transcript.pop("ss01a", None)  # Already subtitled control.
    if len(transcript) != 38:
        raise ValueError("The track list must contain all 38 tracks.")
    reviewed = json.loads(args.review.read_text(encoding="utf-8"))
    en, hu, files = [], [], {}
    for symbol, record in reviewed.items():
        track = transcript[symbol]
        if not track["peak"] or not record["cues"]:
            raise ValueError(f"Silent track or missing subtitle cues: {symbol}")
        filename = Path(record["asset"])
        if filename.name != str(filename) or filename.suffix != ".trb":
            raise ValueError(f"Invalid scene filename: {filename}")
        previous_end = 0
        english, hungarian = [], []
        for start, end, en_text, hu_text in record["cues"]:
            if not previous_end <= start < end <= track["duration_ms"]:
                raise ValueError(f"Invalid subtitle timing: {symbol} {start}–{end}")
            if any(not text.strip() or "\0" in text for text in (en_text, hu_text)):
                raise ValueError(f"Missing or invalid subtitle text: {symbol}")
            english.append((start, end, en_text))
            hungarian.append((start, end, hu_text))
            previous_end = end
        en.append((symbol, english))
        hu.append((symbol, hungarian))
        patched = Path("ScriptedSequence") / filename
        if patched not in files:
            files[patched] = with_subs((data / patched).read_bytes())

    languages = {"ENG": en, "GBR": en, "HUN": hu}
    for folder in (data / "Subs").iterdir():
        if folder.is_dir():
            languages.setdefault(folder.name, [])
    for language, entries in sorted(languages.items()):
        contents = locale.build_subs(0x100, entries)
        if locale.parse_subs(locale.TRB(contents)) != (0x100, entries):
            raise ValueError(f"Generated subtitle TRB failed verification: {language}")
        for symbol, rows in entries:
            if locale.engine_subtitle(contents, symbol) != rows:
                raise ValueError(f"The game would read different subtitle cues: {symbol} ({language})")
        files[Path("Subs") / language / (LIBRARY + ".trb")] = contents

    write_output(args.out, {Path("Data") / name: value for name, value in files.items()})
    silent = [name for name, track in transcript.items() if not track["peak"]]
    unconfirmed = [name for name in transcript if name not in reviewed and name not in silent]
    print(json.dumps({"subtitle_tracks": list(reviewed), "silent": silent,
                      "unconfirmed": unconfirmed, "files": len(files)}, indent=2))


if __name__ == "__main__":
    main()
