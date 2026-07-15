from __future__ import annotations

from dataclasses import dataclass, field, replace
from pathlib import Path
import re
import subprocess
import tempfile

import lief

from .plugin import HookAction

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SEGMENT_RE = re.compile(r"^\s*#\s*define\s+SEGMENT_NAME\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", re.MULTILINE)


@dataclass
class PluginBlob:
    text: bytes
    extra: bytes
    declarations: list[HookAction] = field(default_factory=list)
    _relocs: list[dict] = field(default_factory=list)
    _section_offsets: dict[str, int] = field(default_factory=dict)
    _symbol_offsets: dict[str, int] = field(default_factory=dict)
    register_args: list[str] | None = None
    entry_offset: int = 0

    @property
    def total_bytes(self) -> int:
        return len(self.text) if not self.extra else ((len(self.text) + 15) & ~15) + len(self.extra)

    def for_action(self, action: HookAction) -> "PluginBlob":
        offset = self._symbol_offsets.get(action.handler, self._symbol_offsets.get("_" + action.handler, 0))
        return replace(self, register_args=action.register_args or None, entry_offset=offset)

    def build(self, text_va: int, data_va: int, target_binary: Path | None = None) -> bytes:
        if target_binary is None or not self._relocs:
            out = bytearray(self.text)
            if self.extra:
                out += b"\x00" * ((-len(out)) % 16)
                out += self.extra
            return bytes(out)
        from .symbols import resolve_plugin_relocs
        text, extra = resolve_plugin_relocs(self.text, self.extra, self._relocs, self._section_offsets, target_binary, text_va, data_va)
        out = bytearray(text)
        if extra:
            out += b"\x00" * ((-len(out)) % 16)
            out += extra
        return bytes(out)


def _sec(obj, name: str):
    return next((s for s in obj.sections if s.name == name), None)


def _parse_int(value: str) -> int | None:
    value = value.strip()
    if not value or value in ("0", "entry"):
        return None
    return int(value, 0)


def _parse_meta(data: bytes) -> list[HookAction]:
    items = []
    for raw in data.split(b"\x00"):
        text = raw.decode("utf-8", "ignore").strip()
        if "|" not in text:
            continue
        parts = text.split("|")
        vals = {}
        for p in parts[1:]:
            if "=" in p:
                k, v = p.split("=", 1)
                vals[k] = v
        regs = [r.strip().lower() for r in vals.get("regs", "").split(",") if r.strip()]
        items.append(HookAction(parts[0], _parse_int(vals.get("addr", "")), vals.get("handler", ""), vals.get("segment", "auto"), regs, int(vals.get("size", "0") or "0", 0), vals.get("data", "")))
    return items


def _init_segment(path: Path) -> str | None:
    text = path.read_text(encoding="utf-8", errors="ignore")
    m = SEGMENT_RE.search(text)
    return m.group(1) if m else None


def _compile_source(path: Path, out_dir: Path) -> Path:
    return path


def _apply_segment(actions: list[HookAction], segment: str | None) -> list[HookAction]:
    found = segment or next((a.segment for a in actions if a.kind == "segment" and a.segment not in ("auto", "armcave", "")), None)
    if not found:
        return [a for a in actions if a.kind != "segment"]
    for action in actions:
        if action.kind != "segment" and action.segment in ("auto", "armcave", ""):
            action.segment = found
    return [a for a in actions if a.kind != "segment"]


def _symbol_offsets(obj, text_sec) -> dict[str, int]:
    out = {}
    base = getattr(text_sec, "virtual_address", 0) or 0
    end = base + len(bytes(text_sec.content))
    for sym in obj.symbols:
        name = getattr(sym, "name", "") or ""
        if not name:
            continue
        val = getattr(sym, "value", 0) or 0
        if base <= val < end:
            out[name] = val - base
            if name.startswith("_"):
                out[name[1:]] = val - base
        elif 0 <= val < len(bytes(text_sec.content)):
            out[name] = val
            if name.startswith("_"):
                out[name[1:]] = val
    return out


def extract_cave_asm() -> tuple[bytes, bytes, bytes]:
    with tempfile.TemporaryDirectory(prefix="armcave-") as td:
        src = Path(td) / "c.cpp"
        src.write_text('#include "armcave.h"\n')
        out = Path(td) / "c.o"
        subprocess.run(["clang++", "-target", "arm64-apple-macosx13.0", "-c", "-Oz", "-fno-stack-protector", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-I", str(PROJECT_ROOT / "plugins"), str(src), "-o", str(out)], check=True, capture_output=True)
        obj = lief.parse(str(out))
        sec = _sec(obj, "__caveasm") if obj else None
        data = bytes(sec.content) if sec else b""
        if len(data) < 12:
            raise RuntimeError("__caveasm section not found")
        return data[:4], data[4:8], data[8:12]


def assemble_aarch64(source: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="armcave-asm-") as td:
        src = Path(td) / "a.s"
        out = Path(td) / "a.o"
        src.write_text(".text\n" + source + "\n", encoding="utf-8")
        subprocess.run(["clang", "-target", "arm64-apple-macosx13.0", "-c", str(src), "-o", str(out)], check=True, capture_output=True, text=True)
        obj = lief.parse(str(out))
        sec = _sec(obj, "__text") if obj else None
        if sec is None:
            raise RuntimeError("assembly output has no __text")
        return bytes(sec.content)


def compile_plugin(path: Path, target_binary: Path | None = None) -> PluginBlob:
    with tempfile.TemporaryDirectory(prefix="armcave-") as td:
        src = _compile_source(path, Path(td))
        out = Path(td) / f"{path.stem}.o"
        if path.suffix != ".cpp":
            raise ValueError(f"plugin must be a .cpp file: {path.name}")
        cmd = ["clang++", "-target", "arm64-apple-macosx13.0", "-c", "-Oz", "-fno-stack-protector", "-std=c++17", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-I", str(PROJECT_ROOT / "plugins")]
        if target_binary is None:
            cmd += ["-ffreestanding", "-fno-builtin"]
        cmd += [str(src), "-o", str(out)]
        subprocess.run(cmd, check=True)
        obj = lief.parse(str(out))
        if obj is None:
            raise RuntimeError(f"failed to parse object: {path}")
        text_sec = _sec(obj, "__text")
        if text_sec is None:
            raise RuntimeError(f"missing __text: {path}")
        text = bytes(text_sec.content)
        extra = bytearray()
        offsets = {}
        for sec in obj.sections:
            if sec.name in ("__text", "__compact_unwind", "__eh_frame", "__armhook", "__armkeep"):
                continue
            if sec.name.startswith("__"):
                extra += b"\x00" * ((-len(extra)) % 8)
                offsets[sec.name] = len(extra)
                extra += bytes(sec.content)
        extra += b"\x00" * ((-len(extra)) % 16)
        relocs = []
        for reloc in text_sec.relocations:
            sym = reloc.symbol
            name = sym.name if sym else None
            val = sym.value if sym else 0
            sec_name = None
            base = getattr(text_sec, "offset", text_sec.virtual_address)
            rel_off = reloc.address - base
            if sym and name and sym.type != lief.MachO.Symbol.TYPE.UNDEFINED:
                for sec in obj.sections:
                    start = sec.virtual_address
                    if start <= val < start + sec.size:
                        sec_name = sec.name
                        val -= start
                        break
            relocs.append({"type": reloc.type, "address": rel_off, "symbol_name": name, "symbol_value": val, "symbol_section": sec_name})
        meta_sec = _sec(obj, "__armhook")
        symbols = _symbol_offsets(obj, text_sec)
        actions = _parse_meta(bytes(meta_sec.content) if meta_sec else b"")
        actions = _apply_segment(actions, _init_segment(path))
        return PluginBlob(text, bytes(extra), actions, relocs, offsets, symbols)
