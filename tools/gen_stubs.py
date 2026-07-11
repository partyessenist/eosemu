#!/usr/bin/env python3
"""
Generate stub definitions for every function the EOS SDK headers declare.

The generated code deliberately re-uses Epic's own EOS_DECLARE_FUNC macro rather
than hand-writing prototypes.  Compiled with EOS_BUILDING_SDK=1 that macro
expands to `extern "C" __declspec(dllexport) <ret> <callconv>`, so struct
packing, calling convention and export decoration are correct by construction.
See CLAUDE.md, "Get the ABI for free".

Usage:
    python tools/gen_stubs.py [--sdk-include third_party/EOSSDK/SDK/Include] [--out src/generated] [--check]

--check exits non-zero if the files on disk differ from what would be generated,
so CI can catch a stale checkout.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Epic's macro definition itself lives here; it is not a declaration.
SKIP_HEADERS = {"eos_base.h"}

# A declaration is always on one line and ends in `;` (verified across 1.16.4).
DECL_RE = re.compile(
    r"EOS_DECLARE_FUNC\(\s*(?P<ret>[^)]+?)\s*\)\s*"
    r"(?P<name>EOS_\w+)\s*\((?P<params>[^;]*?)\)\s*;"
)

BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT_RE = re.compile(r"//[^\n]*")

BANNER = """\
// ============================================================================
//  GENERATED FILE -- DO NOT EDIT BY HAND.
//
//  Regenerate with:  python tools/gen_stubs.py
//
//  Every function the EOS SDK declares must be exported or the host process
//  fails to load.  These are placeholders: they trace the call and return an
//  inert value.  To implement one for real, add its name to
//  tools/hand_implemented.txt and define it in a hand-written source file.
// ============================================================================
"""


def strip_comments(text: str) -> str:
    text = BLOCK_COMMENT_RE.sub(" ", text)
    return LINE_COMMENT_RE.sub(" ", text)


def result_for(name: str, params: str) -> str:
    """The EOS_EResult a stub returns, per CLAUDE.md's stub policy.

    Prefer a plausible empty success over EOS_NotImplemented where a consumer is
    likely to hard-fail on error, but never lie in two cases:

    * AntiCheat must never report success -- a false "clean" verdict to a remote
      authority is the one thing a stub must not manufacture (CLAUDE.md).
    * A Copy*/Get* that fills an out-parameter we leave null must NOT return
      Success, or the caller dereferences null. Those keep EOS_NotImplemented so
      the caller sees the failure and skips the out value. Out-params are
      recognised by a `**` (Copy handles/structs) or an `Out`/`bOut` parameter
      name.
    """
    # Functions the header documents as returning a specific code, regardless of
    # the general heuristic below.
    forced = {
        # eos_ui.h: "has an empty implementation (i.e. returns EOS_NotImplemented)
        # on all non-console platforms." Match that exactly.
        "EOS_UI_PrePresent": "EOS_EResult::EOS_NotImplemented",
    }
    if name in forced:
        return forced[name]
    if name.startswith("EOS_AntiCheat"):
        return "EOS_EResult::EOS_NotImplemented"
    if "**" in params or " Out" in params or " bOut" in params:
        return "EOS_EResult::EOS_NotImplemented"
    # Fire-and-forget commands and config setters: report success so a consumer
    # that hard-fails on error keeps going.
    return "EOS_EResult::EOS_Success"


def return_expression(func: dict) -> str | None:
    """The inert value a stub returns. None means the function returns void."""
    r = func["ret"].strip()
    if r == "void":
        return None
    if r == "EOS_EResult":
        return result_for(func["name"], func["params"])
    if r == "EOS_NotificationId":
        return "EOS_INVALID_NOTIFICATIONID"
    if r == "EOS_Bool":
        return "EOS_FALSE"
    if r.endswith("*"):
        return "nullptr"
    # Opaque handles are pointer typedefs, so they do not end in '*'.
    if r.startswith("EOS_H") or r in (
        "EOS_ProductUserId",
        "EOS_EpicAccountId",
        "EOS_ContinuanceToken",
    ):
        return "nullptr"
    # Scalars, typedefs and enum classes all accept static_cast<T>(0).
    return f"static_cast<{r}>(0)"


COMPLETION_RE = re.compile(r"const\s+EOS_\w*Callback\w*\s+(?P<cb>\w+)\s*$")


def completion_delegate(func: dict) -> str | None:
    """The trailing completion-delegate parameter name of a void async op.

    An async stub that never fires its delegate strands any consumer that
    gates progress on the callback (EOS_EResult_IsOperationComplete tells it
    to keep waiting). Such stubs post an EOS_NotImplemented completion via
    EOSEmu::StubComplete instead. Requires the conventional trailing
    `void* ClientData, const EOS_*Callback* <name>` pair.
    """
    if func["ret"].strip() != "void":
        return None
    if "void* ClientData" not in func["params"]:
        return None
    m = COMPLETION_RE.search(func["params"])
    return m.group("cb") if m else None


def include_for(header: Path) -> str:
    """The SDK header a generated translation unit must include."""
    # The three deprecated .inl declarations are pulled in by their _types.h.
    if header.suffix == ".inl":
        return header.name.replace("_deprecated.inl", ".h")
    return header.name


def out_name_for(header: Path) -> str:
    return header.name.replace(".inl", "").replace(".h", "") + ".gen.cpp"


def parse_header(path: Path) -> list[dict]:
    src = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    out = []
    for m in DECL_RE.finditer(src):
        params = " ".join(m.group("params").split())
        out.append(
            {
                "ret": m.group("ret").strip(),
                "name": m.group("name"),
                "params": params,
            }
        )
    return out


def render(header: Path, funcs: list[dict]) -> str:
    lines = [BANNER, "", '#include "core/Stub.h"', "", f'#include "{include_for(header)}"', ""]
    for f in funcs:
        params = f["params"] if f["params"] else ""
        expr = return_expression(f)
        cb = completion_delegate(f)
        lines.append(f'EOS_DECLARE_FUNC({f["ret"]}) {f["name"]}({params})')
        lines.append("{")
        lines.append("\tEOSEMU_API_TRACE();")
        lines.append("\tEOSEMU_STUB();")
        if cb is not None:
            lines.append(f"\t::EOSEmu::StubComplete({cb}, ClientData);")
        if expr is not None:
            lines.append(f"\treturn {expr};")
        lines.append("}")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk-include", default=str(ROOT / "third_party" / "EOSSDK" / "SDK" / "Include"))
    ap.add_argument("--out", default=str(ROOT / "src" / "generated"))
    ap.add_argument("--check", action="store_true", help="verify on-disk files are current")
    args = ap.parse_args()

    inc = Path(args.sdk_include).resolve()
    out_dir = Path(args.out).resolve()
    if not inc.is_dir():
        print(f"error: SDK include dir not found: {inc}", file=sys.stderr)
        return 2

    skip_file = HERE / "hand_implemented.txt"
    hand: set[str] = set()
    if skip_file.exists():
        for line in skip_file.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                hand.add(line)

    headers = sorted(
        [p for p in inc.glob("*.h") if p.name not in SKIP_HEADERS] + list(inc.glob("*.inl"))
    )

    total = 0
    skipped = 0
    seen: dict[str, str] = {}
    generated: dict[Path, str] = {}

    for h in headers:
        funcs = parse_header(h)
        if not funcs:
            continue
        for f in funcs:
            if f["name"] in seen:
                print(
                    f"error: {f['name']} declared twice "
                    f"({seen[f['name']]} and {h.name})",
                    file=sys.stderr,
                )
                return 2
            seen[f["name"]] = h.name
        emit = [f for f in funcs if f["name"] not in hand]
        skipped += len(funcs) - len(emit)
        total += len(funcs)
        if emit:
            generated[out_dir / out_name_for(h)] = render(h, emit)

    unknown = hand - set(seen)
    if unknown:
        print(
            "error: hand_implemented.txt names functions the SDK does not declare: "
            + ", ".join(sorted(unknown)),
            file=sys.stderr,
        )
        return 2

    if args.check:
        stale = []
        existing = set(out_dir.glob("*.gen.cpp"))
        for path, text in generated.items():
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(path.name)
        for path in sorted(existing - set(generated)):
            stale.append(f"{path.name} (orphaned)")
        if stale:
            print("error: generated sources are stale: " + ", ".join(sorted(stale)), file=sys.stderr)
            return 1
        print(f"ok: {len(generated)} generated sources current ({total} declarations)")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    for path in out_dir.glob("*.gen.cpp"):
        if path not in generated:
            path.unlink()
    for path, text in generated.items():
        path.write_text(text, encoding="utf-8", newline="\n")

    print(
        f"generated {len(generated)} files, {total - skipped} stubs "
        f"({total} declarations, {skipped} hand-implemented)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
