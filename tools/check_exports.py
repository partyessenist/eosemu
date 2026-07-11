#!/usr/bin/env python3
"""
Diff a built EOSEmu DLL's export table against the functions the SDK headers
declare.

A host process that imports a symbol we do not export fails to *load* -- there
is no runtime error to catch and no graceful degradation. So this runs as a
post-build step rather than as a test.

On Win32 (x86) exports carry __stdcall decoration (`_EOS_Initialize@4`); this
strips it before comparing.

Usage:
    python tools/check_exports.py <path-to-dll> [--sdk-include third_party/EOSSDK/SDK/Include]
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

DECL_RE = re.compile(
    r"EOS_DECLARE_FUNC\(\s*[^)]+?\s*\)\s*(?P<name>EOS_\w+)\s*\([^;]*?\)\s*;"
)
BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")

SKIP_HEADERS = {"eos_base.h"}

# Exported by Epic's shipping DLL but declared in no header, so nothing that
# links against the headers can reference them. Recorded for completeness.
UNDOCUMENTED_PREFIXES = ("EOS_Audio_", "EOS_BroadcastAudio_", "EOS_RTCVideo_", "EOS_Mercury_")
UNDOCUMENTED_EXACT = {"EOS_BeginScopeEvent", "EOS_EndScopeEvent"}


def pe_exports(path: Path) -> list[str]:
    """Read the export-name table out of a PE image."""
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise ValueError(f"{path} is not a PE image")

    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    is_pe32_plus = magic == 0x20B

    data_dirs = opt + (112 if is_pe32_plus else 96)
    export_rva = struct.unpack_from("<I", data, data_dirs)[0]
    if export_rva == 0:
        return []

    sections = []
    sec_table = opt + opt_size
    for i in range(section_count):
        base = sec_table + 40 * i
        virt_size = struct.unpack_from("<I", data, base + 8)[0]
        virt_addr = struct.unpack_from("<I", data, base + 12)[0]
        raw_ptr = struct.unpack_from("<I", data, base + 20)[0]
        sections.append((virt_addr, virt_size, raw_ptr))

    def to_offset(rva: int) -> int:
        for virt_addr, virt_size, raw_ptr in sections:
            if virt_addr <= rva < virt_addr + max(virt_size, 1) + 0x1000:
                return raw_ptr + (rva - virt_addr)
        raise ValueError(f"RVA {rva:#x} outside all sections")

    export_dir = to_offset(export_rva)
    name_count = struct.unpack_from("<I", data, export_dir + 24)[0]
    names_rva = struct.unpack_from("<I", data, export_dir + 32)[0]

    names_off = to_offset(names_rva)
    out = []
    for i in range(name_count):
        name_rva = struct.unpack_from("<I", data, names_off + 4 * i)[0]
        off = to_offset(name_rva)
        end = data.index(b"\0", off)
        out.append(data[off:end].decode("ascii"))
    return out


def undecorate(symbol: str) -> str:
    """`_EOS_Initialize@4` -> `EOS_Initialize` (Win32 __stdcall)."""
    if symbol.startswith("_") and "@" in symbol:
        return symbol[1:].split("@", 1)[0]
    return symbol


def declared_functions(include_dir: Path) -> set[str]:
    names: set[str] = set()
    headers = [p for p in include_dir.glob("*.h") if p.name not in SKIP_HEADERS]
    headers += list(include_dir.glob("*.inl"))
    for header in headers:
        text = header.read_text(encoding="utf-8", errors="replace")
        text = BLOCK_COMMENT_RE.sub(" ", text)
        text = LINE_COMMENT_RE.sub(" ", text)
        for m in DECL_RE.finditer(text):
            names.add(m.group("name"))
    return names


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dll", type=Path)
    ap.add_argument("--sdk-include", default=str(ROOT / "third_party" / "EOSSDK" / "SDK" / "Include"))
    args = ap.parse_args()

    include_dir = Path(args.sdk_include).resolve()
    required = declared_functions(include_dir)
    if not required:
        print(f"error: parsed no declarations from {include_dir}", file=sys.stderr)
        return 2

    raw = pe_exports(args.dll)
    exported = {undecorate(s) for s in raw}

    missing = sorted(required - exported)
    extra = sorted(
        s
        for s in exported - required
        if not s.startswith(UNDOCUMENTED_PREFIXES) and s not in UNDOCUMENTED_EXACT
    )

    if missing:
        print(
            f"FAIL: {len(missing)} of {len(required)} declared functions are not exported.",
            file=sys.stderr,
        )
        print("      A host process importing any of these will fail to load.", file=sys.stderr)
        for name in missing[:25]:
            print(f"        missing: {name}", file=sys.stderr)
        if len(missing) > 25:
            print(f"        ... and {len(missing) - 25} more", file=sys.stderr)
        return 1

    decorated = any("@" in s for s in raw)
    print(
        f"ok: {args.dll.name} exports all {len(required)} declared EOS functions"
        f"{' (stdcall-decorated)' if decorated else ''}"
    )
    if extra:
        print(f"note: {len(extra)} unexpected extra export(s): {', '.join(extra[:8])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
