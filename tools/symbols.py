from __future__ import annotations

from pathlib import Path
import struct

import lief


def _indirect(binary):
    for cmd in binary.commands:
        if cmd.command == lief.MachO.LoadCommand.TYPE.DYSYMTAB:
            return list(cmd.indirect_symbols)
    return None


def _names(symbol_name: str) -> list[str]:
    return [symbol_name, symbol_name[1:] if symbol_name.startswith("_") else "_" + symbol_name, "__" + symbol_name.lstrip("_")]


def _resolve_armcave_va(symbol_name: str) -> int | None:
    for name in _names(symbol_name):
        marker = "armcave_va_"
        idx = name.find(marker)
        if idx < 0:
            continue
        raw = name[idx + len(marker):]
        try:
            return int(raw, 0)
        except ValueError:
            return None
    return None


def _resolve_via_symbol_table(binary: lief.MachO.Binary, symbol_name: str) -> int | None:
    stubs = next((s for s in binary.sections if s.name == "__stubs"), None)
    indirect = _indirect(binary)
    if stubs is None or indirect is None:
        return None
    names = _names(symbol_name)
    size = stubs.reserved2 or 12
    for i in range(stubs.size // size):
        idx = stubs.reserved1 + i
        if idx < len(indirect) and indirect[idx].name in names:
            return stubs.virtual_address + i * size
    return None


def _resolve_import_slot(binary: lief.MachO.Binary, symbol_name: str) -> int | None:
    indirect = _indirect(binary)
    if indirect is None:
        return None
    names = set(_names(symbol_name))
    for section in binary.sections:
        if section.name not in ("__got", "__la_symbol_ptr", "__nl_symbol_ptr"):
            continue
        for i in range(section.size // 8):
            idx = section.reserved1 + i
            if idx < len(indirect) and indirect[idx].name in names:
                return section.virtual_address + i * 8
    return None


def list_available_symbols(binary_path: Path) -> list[dict]:
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    out = []
    stubs = next((s for s in binary.sections if s.name == "__stubs"), None)
    indirect = _indirect(binary)
    if stubs is None:
        return out
    size = stubs.reserved2 or 12
    for i in range(stubs.size // size):
        idx = stubs.reserved1 + i
        name = indirect[idx].name if indirect and idx < len(indirect) else None
        if name:
            va = stubs.virtual_address + i * size
            out.append({"name": name, "address": f"0x{va:x}", "kind": "imported", "signature": "", "desc": f"0x{va:x}"})
    return out


def _patch_branch26(data: bytearray, off: int, src: int, dst: int) -> None:
    imm = (dst - src) >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError("branch relocation out of range")
    insn = struct.unpack_from("<I", data, off)[0]
    struct.pack_into("<I", data, off, (insn & 0xFC000000) | (imm & 0x03FFFFFF))


def _patch_page21(data: bytearray, off: int, pc: int, target: int) -> None:
    diff = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12
    imm = diff & 0x1FFFFF
    insn = struct.unpack_from("<I", data, off)[0]
    struct.pack_into("<I", data, off, (insn & 0x9F00001F) | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5))


def _patch_pageoff12(data: bytearray, off: int, target: int) -> None:
    insn = struct.unpack_from("<I", data, off)[0]
    struct.pack_into("<I", data, off, (insn & 0xFFC003FF) | ((target & 0xFFF) << 10))


def _patch_got_load_pageoff12(data: bytearray, off: int, target: int) -> None:
    insn = struct.unpack_from("<I", data, off)[0]
    scale = 8 if (insn & 0xC0000000) == 0xC0000000 else 4
    imm = (target & 0xFFF) // scale
    struct.pack_into("<I", data, off, (insn & 0xFFC003FF) | (imm << 10))


def resolve_plugin_relocs(text: bytes, extra: bytes, relocs: list[dict], offsets: dict[str, int], binary_path: Path, text_va: int, data_va: int) -> tuple[bytes, bytes]:
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    text_buf = bytearray(text)
    extra_buf = bytearray(extra)
    for r in relocs:
        t, off, name, val, section = int(r["type"]), r["address"], r["symbol_name"], r["symbol_value"], r["symbol_section"]
        if t == 2 and name:
            if val == 0 and section is None:
                dst = _resolve_armcave_va(name)
                if dst is None:
                    dst = _resolve_via_symbol_table(binary, name)
            else:
                dst = text_va + val
            if dst is None:
                raise RuntimeError(f"unresolved symbol: {name}")
            _patch_branch26(text_buf, off, text_va + off, dst)
        elif t == 3 and section:
            target = text_va + val if section == "__text" else data_va + offsets.get(section, 0) + val
            _patch_page21(text_buf, off, text_va + off, target)
        elif t == 4 and section:
            target = text_va + val if section == "__text" else data_va + offsets.get(section, 0) + val
            _patch_pageoff12(text_buf, off, target)
        elif t == 5 and name:
            target = _resolve_import_slot(binary, name)
            if target is None:
                raise RuntimeError(f"unresolved import slot: {name}")
            _patch_page21(text_buf, off, text_va + off, target)
        elif t == 6 and name:
            target = _resolve_import_slot(binary, name)
            if target is None:
                raise RuntimeError(f"unresolved import slot: {name}")
            _patch_got_load_pageoff12(text_buf, off, target)
    return bytes(text_buf), bytes(extra_buf)
