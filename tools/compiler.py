from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import subprocess
import tempfile

import lief

PROJECT_ROOT = Path(__file__).resolve().parent.parent


@dataclass
class PluginBlob:
    text: bytes
    extra: bytes
    # Relocation info extracted from .o file for later patching
    _relocs: list[dict] = field(default_factory=list)
    # Section map: name -> offset in extra (for local reloc patching)
    _section_offsets: dict[str, int] = field(default_factory=dict)

    @property
    def total_bytes(self) -> int:
        if not self.extra:
            return len(self.text)
        aligned_text = (len(self.text) + 15) & ~15
        return aligned_text + len(self.extra)

    def build(self, text_va: int, data_va: int, target_binary: Path | None = None) -> bytes:
        """Return the final blob bytes with relocations patched."""
        if target_binary is None or not self._relocs:
            result = bytearray(self.text)
            if self.extra:
                pad = (-len(self.text)) % 16
                result += b"\x00" * pad
                result += self.extra
            return bytes(result)

        from .symbols import resolve_plugin_relocs

        patched_text, patched_extra = resolve_plugin_relocs(
            self.text, self.extra, self._relocs, self._section_offsets,
            target_binary, text_va, data_va,
        )

        result = bytearray(patched_text)
        if patched_extra:
            pad = (-len(patched_text)) % 16
            result += b"\x00" * pad
            result += patched_extra
        return bytes(result)


def extract_cave_asm() -> tuple[bytes, bytes, bytes]:
    plugins_dir = PROJECT_ROOT / "plugins"
    with tempfile.TemporaryDirectory(prefix="armcave-") as td:
        src = Path(td) / "_cave_extract.c"
        src.write_text('#include "armcave.h"\n')
        out = Path(td) / "_cave_extract.o"
        subprocess.run(
            ["clang", "-target", "arm64-apple-macosx13.0", "-c", "-Oz",
             "-fno-stack-protector", "-I", str(plugins_dir),
             str(src), "-o", str(out)],
            check=True, capture_output=True,
        )
        obj = lief.parse(str(out))
        if obj is None:
            raise RuntimeError("failed to parse cave asm object")
        sec = next((s for s in obj.sections if s.name == "__caveasm"), None)
        if sec is None:
            raise RuntimeError("__caveasm section not found in compiled output")
        data = bytes(sec.content)
        if len(data) < 12:
            raise RuntimeError(f"__caveasm section too small: {len(data)} bytes")
        return data[0:4], data[4:8], data[8:12]


def compile_plugin(path: Path, target_binary: Path | None = None) -> PluginBlob:
    with tempfile.TemporaryDirectory(prefix="armcave-") as td:
        out = Path(td) / f"{path.stem}.o"

        cmd = [
            "clang",
            "-target", "arm64-apple-macosx13.0",
            "-c", "-Oz",
            "-fno-stack-protector",
        ]

        if target_binary is not None:
            cmd += [
                "-I", str(PROJECT_ROOT / "plugins"),
                "-include", "armcave.h",
                "-Wno-implicit-function-declaration",
            ]
        else:
            cmd += ["-ffreestanding", "-fno-builtin"]

        cmd += [str(path), "-o", str(out)]

        subprocess.run(cmd, check=True)
        obj = lief.parse(str(out))
        if obj is None:
            raise RuntimeError(f"failed to parse object file for {path}")

        text_sec = next((s for s in obj.sections if s.name == "__text"), None)
        if text_sec is None:
            raise RuntimeError(f"missing __text in {path}")

        text_bytes = bytes(text_sec.content)

        # Collect extra data sections (strings, const, etc.)
        extra = bytearray()
        section_offsets: dict[str, int] = {}
        for sec in obj.sections:
            if sec.name in ("__text", "__compact_unwind", "__eh_frame"):
                continue
            if sec.name.startswith("__"):
                pad = (-len(extra)) % 8
                if pad:
                    extra += b"\x00" * pad
                section_offsets[sec.name] = len(extra)
                extra += bytes(sec.content)
        if extra:
            pad = (-len(extra)) % 16
            if pad:
                extra += b"\x00" * pad

        extra_bytes = bytes(extra)

        if target_binary is None:
            return PluginBlob(text=text_bytes, extra=b"")

        # ── build section VA ranges for local symbol resolution ──
        section_ranges: list[tuple[int, int, str]] = []
        for sec in obj.sections:
            va = sec.virtual_address
            section_ranges.append((va, va + sec.size, sec.name))

        # ── extract relocations from the .o file ──
        relocs: list[dict] = []
        for reloc in text_sec.relocations:
            sym = reloc.symbol
            sym_name = sym.name if sym else None
            sym_type = sym.type if sym else None
            sym_value = sym.value if sym else 0
            sym_section_name = None
            sym_section_offset = sym_value  # section-relative offset; default = raw value

            # LIEF reports reloc.address as file_offset + section-relative addr for .o files
            sec_base = getattr(text_sec, 'offset', text_sec.virtual_address) if hasattr(text_sec, 'offset') else text_sec.virtual_address
            rel_off = reloc.address - sec_base

            # Determine which section the symbol lives in.
            # LIEF's section_number isn't reliably usable, so match by VA range.
            # UNDEFINED symbols (external imports) have value=0 and belong to no section.
            if sym and sym_name and sym.type != lief.MachO.Symbol.TYPE.UNDEFINED:
                # Try section_number first
                sn = None
                try:
                    sn_val = sym.section_number
                    if isinstance(sn_val, int) and 0 < sn_val <= len(obj.sections):
                        sn = sn_val
                except Exception:
                    pass
                if sn is not None:
                    sym_section_name = obj.sections[sn - 1].name
                    # sym_value is already section-relative for section_number symbols
                else:
                    # Fallback: match by VA range
                    for va_start, va_end, sec_name in section_ranges:
                        if va_start <= sym_value < va_end:
                            sym_section_name = sec_name
                            sym_section_offset = sym_value - va_start
                            break

            relocs.append({
                "type": reloc.type,
                "address": rel_off,
                "symbol_name": sym_name,
                "symbol_type": sym_type,
                "symbol_value": sym_section_offset,
                "symbol_section": sym_section_name,
            })

        return PluginBlob(
            text=text_bytes,
            extra=extra_bytes,
            _relocs=relocs,
            _section_offsets=section_offsets,
        )
