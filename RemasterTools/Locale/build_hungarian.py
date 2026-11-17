"""Build the Hungarian PC game overlay from its source tables and the owner's game data."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True, help="Original Barnyard PC Data directory")
    parser.add_argument("--out", type=Path, required=True, help="New directory for the finished overlay")
    args = parser.parse_args()
    data = args.data.resolve()
    output = args.out.resolve()
    if output.exists() or output == data or data in output.parents:
        parser.error("--out must be a new directory outside the original Data directory")
    source = Path(__file__).resolve().parent
    base = source / "hu_strings.json"
    extra = source / "hu_remaster.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="barnyard-locale-", dir=output.parent) as temp:
        work = Path(temp)
        overlay = work / "overlay"
        additional = work / "subtitles"
        subprocess.run([sys.executable, "-B", str(source / "locale_tool.py"), "--data", str(data),
                        "import", "--lang", "hun", "--in", str(base), str(extra),
                        "--out", str(overlay)], check=True)
        subprocess.run([sys.executable, "-B", str(source / "extra_subtitles.py"),
                        "--data", str(data), "--out", str(additional)], check=True)
        for item in (additional / "Data").rglob("*"):
            if item.is_file():
                dest = overlay / item.relative_to(additional)
                dest.parent.mkdir(parents=True, exist_ok=True)
                if dest.exists():
                    raise ValueError(f"Duplicate generated file: {dest.name}")
                shutil.copyfile(item, dest)
        catalog = json.loads(base.read_text(encoding="utf-8"))
        catalog.update(json.loads(extra.read_text(encoding="utf-8")))
        (overlay / "Data/Locale/hun_mod.json").write_bytes(
            (json.dumps(catalog, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        (overlay / "mod_strings_hun.json").unlink()
        (overlay / "missing_hun.txt").unlink()
        overlay.rename(output)


if __name__ == "__main__":
    main()
