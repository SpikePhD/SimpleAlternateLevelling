#!/usr/bin/env python3
"""Verify exported SKSE runtime and structure compatibility flags in a PE DLL."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def rva_to_offset(data: bytes, rva: int, sections: list[tuple[int, int, int]]) -> int:
    for virtual_address, virtual_size, raw_offset, raw_size in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            offset = raw_offset + (rva - virtual_address)
            if offset >= len(data):
                break
            return offset
    raise ValueError(f"RVA 0x{rva:X} is not backed by PE section data")


def find_export(data: bytes, name: str) -> int:
    if data[:2] != b"MZ":
        raise ValueError("Input is not a PE image (missing MZ header)")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("Input is not a PE image (missing PE signature)")

    coff_offset = pe_offset + 4
    _, section_count, _, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", data, coff_offset
    )
    optional_offset = coff_offset + 20
    magic = struct.unpack_from("<H", data, optional_offset)[0]
    if magic == 0x20B:
        directory_offset = optional_offset + 112
    elif magic == 0x10B:
        directory_offset = optional_offset + 96
    else:
        raise ValueError(f"Unsupported PE optional-header magic 0x{magic:X}")

    export_rva, export_size = struct.unpack_from("<II", data, directory_offset)
    if not export_rva or not export_size:
        raise ValueError("DLL has no export directory")

    section_table = optional_offset + optional_size
    sections = []
    for index in range(section_count):
        section_offset = section_table + index * 40
        _, virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<8sIIII", data, section_offset
        )
        sections.append((virtual_address, virtual_size, raw_offset, raw_size))

    export_offset = rva_to_offset(data, export_rva, sections)
    fields = struct.unpack_from("<IIHHIIIIIII", data, export_offset)
    function_count, name_count = fields[6], fields[7]
    functions_rva, names_rva, ordinals_rva = fields[8], fields[9], fields[10]
    functions_offset = rva_to_offset(data, functions_rva, sections)
    names_offset = rva_to_offset(data, names_rva, sections)
    ordinals_offset = rva_to_offset(data, ordinals_rva, sections)

    for index in range(name_count):
        name_rva = struct.unpack_from("<I", data, names_offset + index * 4)[0]
        name_offset = rva_to_offset(data, name_rva, sections)
        end = data.find(b"\0", name_offset)
        if end == -1:
            raise ValueError("PE export name is not null-terminated")
        exported_name = data[name_offset:end].decode("ascii")
        if exported_name == name:
            ordinal_index = struct.unpack_from("<H", data, ordinals_offset + index * 2)[0]
            if ordinal_index >= function_count:
                raise ValueError("Export ordinal is outside the function table")
            function_rva = struct.unpack_from("<I", data, functions_offset + ordinal_index * 4)[0]
            if export_rva <= function_rva < export_rva + export_size:
                raise ValueError(f"{name} is a forwarded export, not version data")
            return rva_to_offset(data, function_rva, sections)

    raise ValueError(f"DLL does not export {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", required=True, type=Path)
    args = parser.parse_args()

    data = args.dll.read_bytes()
    offset = find_export(data, "SKSEPlugin_Version")
    if offset + 0x350 > len(data):
        raise ValueError("SKSE plugin version export is truncated")

    version_independence_ex = struct.unpack_from("<I", data, offset + 0x304)[0]
    version_independence = struct.unpack_from("<I", data, offset + 0x308)[0]
    address_library_v5 = bool(version_independence_ex & 0x2)
    address_library = bool(version_independence & 0x1)
    post_629_structs = bool(version_independence & 0x4)

    print(
        "SKSE metadata flags: "
        f"AddressLibraryV5={address_library_v5}, "
        f"AddressLibrary={address_library}, "
        f"post-1.6.629-structures={post_629_structs}"
    )
    if not (address_library_v5 and address_library and post_629_structs):
        raise SystemExit("SKSE metadata is missing required runtime compatibility flags")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
