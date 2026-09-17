"""Audit PE architecture, runtime imports and known post-Windows-7 APIs.

This is a build check; Windows 7 hardware/VM acceptance is still required.
"""
import argparse
import json
from pathlib import Path
import struct


SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "comctl32.dll", "comdlg32.dll", "crypt32.dll", "gdi32.dll",
    "iphlpapi.dll", "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll", "oleaut32.dll",
    "rpcrt4.dll", "secur32.dll", "shell32.dll", "user32.dll", "ws2_32.dll", "winmm.dll",
    "winspool.drv", "winhttp.dll", "normaliz.dll", "ncrypt.dll",
}
POST_WIN7_APIS = {
    "GetDpiForWindow", "SetProcessDpiAwareness", "SetProcessDpiAwarenessContext",
    "GetSystemTimePreciseAsFileTime", "GetTempPath2W", "GetTempPath2A", "WaitOnAddress",
    "WakeByAddressSingle", "WakeByAddressAll", "SetThreadDescription", "GetThreadDescription",
    "GetThreadInformation", "SetThreadInformation", "IsWow64Process2", "CreateFile2",
    "GetAddrInfoExCancel", "SetInterfaceDnsSettings", "GetInterfaceDnsSettings",
}


def inspect(data):
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError("Truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    def u16(offset):
        return unpack("<H", offset)[0]

    def u32(offset):
        return unpack("<I", offset)[0]

    pe = u32(0x3C)
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Not a PE file")
    machine, count = unpack("<HH", pe + 4)
    optional, optional_size = pe + 24, u16(pe + 20)
    magic = u16(optional)
    if magic not in (0x10B, 0x20B):
        raise ValueError("Unsupported PE optional header")
    is64 = magic == 0x20B
    if optional_size < (224 if is64 else 208):
        raise ValueError("Missing PE data directories")
    directories = optional + (112 if is64 else 96)
    sections = []
    for index in range(count):
        offset = optional + optional_size + index * 40
        _, rva, raw_size, raw = unpack("<IIII", offset + 8)
        sections.append((rva, raw_size, raw))

    def file_offset(rva):
        for begin, size, raw in sections:
            if begin <= rva < begin + size:
                offset = raw + rva - begin
                if offset < len(data):
                    return offset
        if rva < min(u32(optional + 60), len(data)):
            return rva
        raise ValueError(f"Unmapped PE address: {rva:x}")

    def cstring(rva):
        offset = file_offset(rva)
        end = data.find(b"\0", offset, min(len(data), offset + 4096))
        if end < 0:
            raise ValueError("Unterminated PE import name")
        return data[offset:end].decode("ascii")

    imports = {}
    import_rva = u32(directories + 8)
    if import_rva:
        cursor = file_offset(import_rva)
        for _ in range(256):
            original, _, _, name, first = unpack("<IIIII", cursor)
            if not name:
                break
            functions = []
            width = 8 if is64 else 4
            thunk = file_offset(original or first)
            for index in range(16384):
                value = unpack("<Q" if is64 else "<I", thunk + index * width)[0]
                if not value:
                    break
                functions.append(f"ordinal:{value & 65535}" if value & (1 << (width * 8 - 1))
                                 else cstring(value + 2))
            else:
                raise ValueError("Unterminated PE import thunk")
            imports.setdefault(cstring(name), []).extend(functions)
            cursor += 20
        else:
            raise ValueError("Unterminated PE import table")

    unexpected = [dll for dll in imports if dll.lower() not in SYSTEM_DLLS]
    new_apis = [name for names in imports.values() for name in names if name in POST_WIN7_APIS]
    subsystem = [u16(optional + 48), u16(optional + 50)]
    delayed = u32(directories + 13 * 8) != 0
    architecture = {0x8664: "x64", 0x14C: "x86"}.get(machine, hex(machine))
    consistent = (machine, magic) in ((0x8664, 0x20B), (0x14C, 0x10B))
    return {
        "architecture": architecture, "subsystem_version": subsystem,
        "system_dlls_only": not unexpected, "unexpected_dlls": unexpected,
        "known_post_win7_imports": new_apis, "delay_imports": delayed, "imports": imports,
        "passed": consistent and subsystem == [6, 1] and not unexpected and not new_apis and not delayed
                  and "msvcrt.dll" in {dll.lower() for dll in imports},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = {"file": str(args.file), **inspect(args.file.read_bytes())}
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(1, f"PE audit failed: {error}\n")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "imports"}, ensure_ascii=False))
    print("DLLs: " + ", ".join(result["imports"]))
    raise SystemExit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()
