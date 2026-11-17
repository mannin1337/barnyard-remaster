"""Restore pinned OpenBarnyard build inputs. All paths are relative to this source tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor


ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads(Path(__file__).with_name("upstream-files.json").read_text(encoding="utf-8"))
UPSTREAM = "https://raw.githubusercontent.com/InfiniteC0re/OpenBarnyard/"


def blob_id(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def fetch(relative):
    url = UPSTREAM + LOCK["commit"] + "/" + urllib.parse.quote(relative)
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=45) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def restore(item, upstream):
    relative, expected = item
    path = (ROOT / relative).resolve()
    if not path.is_relative_to(ROOT) or path == ROOT:
        raise ValueError("Invalid dependency path in manifest")
    if path.exists():
        if blob_id(path.read_bytes()) != expected:
            raise ValueError("Existing file differs from the pin: " + relative)
        return 0
    if upstream:
        data = subprocess.check_output(["git", "-C", str(upstream), "cat-file", "blob", expected])
    else:
        data = fetch(relative)
    if blob_id(data) != expected:
        raise ValueError("Upstream hash mismatch: " + relative)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
        try:
            tmp.write(data)
            tmp.close()
            os.replace(tmp.name, path)
        finally:
            Path(tmp.name).unlink(missing_ok=True)
    return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream-directory", type=Path, help="Optional existing upstream Git checkout for offline builds")
    args = parser.parse_args()
    with ThreadPoolExecutor(max_workers=2) as workers:
        count = sum(workers.map(lambda item: restore(item, args.upstream_directory), LOCK["files"].items()))
    print(f"Verified {len(LOCK['files'])} upstream build inputs, restored {count}.")


if __name__ == "__main__":
    main()
