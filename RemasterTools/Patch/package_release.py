#!/usr/bin/env python3
"""Maintainer utility. Package a public source build with the exact verified DX11 cache."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zipfile

HERE = Path(__file__).resolve().parent
PUBLIC = ("install.py", "locale_binary.py", "files.json", "translations.json", "INSTALL.md")
PRIVATE_IP = re.compile(rb"(?<![\w.])(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}(?![\w.])")


def fingerprint(data):
    return [len(data), hashlib.sha256(data).hexdigest()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="Final public OpenBarnyard Win32 DX11 Release build")
    parser.add_argument("--cache", type=Path, required=True, help="Verified embedded shader cache directory")
    parser.add_argument("--output", type=Path, required=True, help="New ZIP filename")
    args = parser.parse_args()
    exe = args.exe.read_bytes()
    pe = int.from_bytes(exe[60:64], "little")
    if exe[:2] != b"MZ" or exe[pe:pe+6] != b"PE\0\0L\1":
        parser.error("Expected a Windows x86 build")
    if PRIVATE_IP.search(exe) or PRIVATE_IP.search(exe.replace(b"\0", b"")):
        parser.error("The supplied executable contains a private network literal. Use the public source build.")
    def bat(body):
        return ("@echo off\r\ncd /d \"%~dp0\"\r\n" + body + "\r\n").encode("ascii")
    program = {
        "OpenBarnyard/OpenBarnyard.exe": exe,
        "Barnyard_inditas.bat": bat('start "" "OpenBarnyard\\OpenBarnyard.exe" %*'),
        "Barnyard_LAN_szerver.bat": bat('start "" "OpenBarnyard\\OpenBarnyard.exe" -nopause -nethost %*'),
        "Barnyard_LAN_csatlakozas.bat": bat('start "" "%~dp0OpenBarnyard\\OpenBarnyard.exe" -nopause -netjoin %*'),
    }
    approved = json.loads((HERE/"shader-hashes.json").read_text(encoding="utf-8"))
    for name, expected in approved.items():
        if not re.fullmatch(r"embedded_[0-9a-f]{16}\.cache", name):
            parser.error("Invalid shader name")
        data = (args.cache/name).read_bytes()
        if data[:4] != b"TSCC" or expected != fingerprint(data):
            parser.error("Invalid shader cache entry: " + name)
        program["OpenBarnyard/ShaderCache/"+name] = data
    payload = {name: (HERE / name).read_bytes() for name in PUBLIC}
    inputs = {name:fingerprint(data) for name,data in payload.items()}
    release = {"version":"5.5", "inputs":inputs, "program":{k:fingerprint(v) for k,v in program.items()}}
    payload["release.json"] = (json.dumps(release, indent=2)+"\n").encode("utf-8")
    payload.update({"program/"+k:v for k,v in program.items()})
    with zipfile.ZipFile(args.output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path, data in sorted(payload.items()):
            entry=zipfile.ZipInfo(path, date_time=(2026,11,17,0,0,0))
            entry.create_system=3
            entry.external_attr=0o100644 << 16
            entry.compress_type=zipfile.ZIP_DEFLATED
            output.writestr(entry, data, compresslevel=9)
    print("EXE SHA-256", fingerprint(exe)[1])
    print("ZIP SHA-256", fingerprint(args.output.read_bytes())[1])


if __name__ == "__main__":
    main()
