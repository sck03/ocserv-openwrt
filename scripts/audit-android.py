#!/usr/bin/env python3
"""Reject wrong-architecture or non-16-KB-aligned Android native libraries/APKs."""
import argparse
from pathlib import Path
import struct
import zipfile

MACHINES = {"armeabi-v7a": (1, 40), "arm64-v8a": (2, 183), "x86_64": (2, 62)}


def audit_elf(data, abi):
    expected_class, expected_machine = MACHINES[abi]
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4:6] != bytes([expected_class, 1]):
        raise ValueError(f"{abi}: invalid ELF class/endianness")
    if struct.unpack_from("<H", data, 18)[0] != expected_machine:
        raise ValueError(f"{abi}: wrong machine type")
    if expected_class == 2:
        offset = struct.unpack_from("<Q", data, 32)[0]
        size, count = struct.unpack_from("<HH", data, 54)
        minimum = 56
    else:
        offset = struct.unpack_from("<I", data, 28)[0]
        size, count = struct.unpack_from("<HH", data, 42)
        minimum = 32
    if size < minimum or not count or offset + size * count > len(data):
        raise ValueError(f"{abi}: invalid program headers")
    loads = 0
    for index in range(count):
        header = offset + index * size
        if struct.unpack_from("<I", data, header)[0] != 1:
            continue
        loads += 1
        if expected_class == 2:
            file_offset, address = struct.unpack_from("<QQ", data, header + 8)
            alignment = struct.unpack_from("<Q", data, header + 48)[0]
        else:
            file_offset, address = struct.unpack_from("<II", data, header + 4)
            alignment = struct.unpack_from("<I", data, header + 28)[0]
        if alignment < 16384 or alignment & (alignment - 1) or (address - file_offset) % 16384:
            raise ValueError(f"{abi}: load segment is not 16-KB aligned")
    if not loads:
        raise ValueError(f"{abi}: missing load segments")


def audit(path):
    if path.is_dir():
        for abi in MACHINES:
            audit_elf((path / abi / "libopenconnect.so").read_bytes(), abi)
    else:
        with zipfile.ZipFile(path) as apk:
            for abi in MACHINES:
                audit_elf(apk.read(f"lib/{abi}/libopenconnect.so"), abi)
    print(f"Native architecture and 16-KB alignment verified: {path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    audit(parser.parse_args().path)
