from __future__ import annotations

from pathlib import Path
import struct

import lief


def _resolve_via_symbol_table(binary: lief.MachO.Binary, symbol_name: str) -> int | None:
    """Look up an imported symbol and return its stub VA.

    Tries multiple name forms to handle macOS C underscore conventions:
    _printf (C) → __printf (symbol), but import table has _printf.
    """
    stubs_sec = next((s for s in binary.sections if s.name == "__stubs"), None)
    if stubs_sec is None:
        return None

    stub_size = stubs_sec.reserved2 or 12
    stub_count = stubs_sec.size // stub_size
    stub_start = stubs_sec.reserved1  # index into indirect symbol table

    indirect = _get_indirect_symbols(binary)
    if indirect is None:
        return None

    # Build name alternatives (macOS C adds leading underscore)
    names_to_try = [symbol_name]
    if symbol_name.startswith("_"):
        names_to_try.append(symbol_name[1:])       # __printf → _printf
        if symbol_name.startswith("__"):
            names_to_try.append(symbol_name[2:])   # __printf → printf
    else:
        names_to_try.append("_" + symbol_name)     # printf → _printf
        names_to_try.append("__" + symbol_name)    # printf → __printf

    for name in names_to_try:
        for i in range(stub_count):
            idx = stub_start + i
            if idx < len(indirect):
                sym = indirect[idx]
                if sym.name == name:
                    return stubs_sec.virtual_address + i * stub_size

    return None


def _get_indirect_symbols(binary: lief.MachO.Binary):
    """Return the indirect symbol table as a list of Symbol objects."""
    for cmd in binary.commands:
        if cmd.command == lief.MachO.LoadCommand.TYPE.DYSYMTAB:
            return list(cmd.indirect_symbols)
    return None


def _stub_va(binary: lief.MachO.Binary, symbol_name: str) -> int | None:
    return _resolve_via_symbol_table(binary, symbol_name)


def list_available_symbols(binary_path: Path) -> list[dict]:
    """Return all symbols callable from injected code.

    Includes:
      - Imported symbols (have stubs in __stubs)
      - Built-in helpers (arm_logf)
    """
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")

    symbols: list[dict] = []

    # Built-in helpers (always available)
    symbols.append({"name": "arm_logf", "address": "built-in", "kind": "builtin",
                    "signature": "void arm_logf(const char *fmt, ...)",
                    "desc": "printf-style format string: %d %u %x %s %c %p, plus %ld etc."})
    symbols.append({"name": "BRANCH_GOTO_DST", "address": "built-in", "kind": "builtin",
                    "signature": "macro",
                    "desc": "Branch-Host: jump to the conditional branch target (destination, requires HOOK_BRANCH_HOST 1)"})
    symbols.append({"name": "BRANCH_GOTO_NEXT", "address": "built-in", "kind": "builtin",
                    "signature": "macro",
                    "desc": "Branch-Host: jump to HOOK+4 (fall-through, requires HOOK_BRANCH_HOST 1)"})
    symbols.append({"name": "BRANCH_GOTO_CONV", "address": "built-in", "kind": "builtin",
                    "signature": "macro",
                    "desc": "Branch-Host: jump to convergence point (where both paths meet, requires HOOK_BRANCH_HOST 1)"})

    stubs_sec = next((s for s in binary.sections if s.name == "__stubs"), None)
    if stubs_sec is None:
        return symbols

    stub_size = stubs_sec.reserved2 or 12
    stub_count = stubs_sec.size // stub_size
    stub_start = stubs_sec.reserved1

    indirect = _get_indirect_symbols(binary)

    for i in range(stub_count):
        sym_name = None
        if indirect is not None:
            idx = stub_start + i
            if idx < len(indirect):
                sym = indirect[idx]
                if sym and sym.name:
                    sym_name = sym.name

        if sym_name:
            va = stubs_sec.virtual_address + i * stub_size
            symbols.append({
                "name": sym_name,
                "address": f"0x{va:x}",
                "kind": "imported",
                "signature": "",
                "desc": f"Stub at 0x{va:x}",
            })

    return symbols


def _encode_adrp_imm(imm21: int) -> int:
    """Encode a 21-bit signed immediate into ADRP instruction form."""
    # immhi = imm21[20:2], immlo = imm21[1:0]
    immlo = imm21 & 0x3
    immhi = (imm21 >> 2) & 0x7FFFF
    return (immlo << 29) | (immhi << 5)


def _encode_add_imm(imm12: int) -> int:
    """Encode a 12-bit unsigned immediate into ADD instruction form."""
    return (imm12 & 0xFFF) << 10


def _patch_branch26(data: bytearray, offset: int, src_va: int, dst_va: int) -> None:
    delta = dst_va - src_va
    if delta % 4:
        raise ValueError(f"branch target not 4-byte aligned: src=0x{src_va:x} dst=0x{dst_va:x}")
    imm26 = delta >> 2
    if not -(1 << 25) <= imm26 < (1 << 25):
        raise ValueError(f"branch target out of range: src=0x{src_va:x} dst=0x{dst_va:x} delta={delta}")
    insn = struct.unpack_from("<I", data, offset)[0]
    insn = (insn & 0xFC000000) | (imm26 & 0x03FFFFFF)
    struct.pack_into("<I", data, offset, insn)


def _patch_page21(data: bytearray, offset: int, pc_va: int, target_va: int) -> None:
    """Patch an ADRP instruction's page offset."""
    pc_page = pc_va & ~0xFFF
    target_page = target_va & ~0xFFF
    page_diff = (target_page - pc_page) >> 12
    insn = struct.unpack_from("<I", data, offset)[0]
    # Keep opcode + rd; replace immediate fields
    insn = (insn & 0x9F00001F) | _encode_adrp_imm(page_diff & 0x1FFFFF)
    struct.pack_into("<I", data, offset, insn)


def _patch_pageoff12(data: bytearray, offset: int, target_va: int) -> None:
    """Patch an ADD instruction's page offset."""
    page_off = target_va & 0xFFF
    insn = struct.unpack_from("<I", data, offset)[0]
    insn = (insn & 0xFFC003FF) | _encode_add_imm(page_off)
    struct.pack_into("<I", data, offset, insn)


def resolve_plugin_relocs(
    text_bytes: bytes,
    extra_bytes: bytes,
    relocs: list[dict],
    section_offsets: dict[str, int],
    binary_path: Path,
    text_va: int,
    data_va: int,
) -> tuple[bytes, bytes]:
    """Resolve relocations in plugin text, return (patched_text, patched_extra).

    text_va  — VA where __text will be placed in the cave
    data_va  — VA where r/o data (strings, const) will be placed
    """
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")

    text_buf = bytearray(text_bytes)
    extra_buf = bytearray(extra_bytes)

    for reloc in relocs:
        rel_type = reloc["type"]
        rel_off = reloc["address"]
        sym_name = reloc["symbol_name"]
        sym_value = reloc["symbol_value"]
        sym_section = reloc["symbol_section"]

        # ARM64 relocation type values: BRANCH26=2, PAGE21=3, PAGEOFF12=4
        if int(rel_type) == 2:  # ARM64_RELOC_BRANCH26
            if sym_name is None:
                continue
            if sym_value == 0 and sym_section is None:
                # External/undefined symbol — resolve to import stub
                stub = _resolve_via_symbol_table(binary, sym_name)
                if stub is None:
                    available = [s["name"] for s in list_available_symbols(binary_path)]
                    hint = f"symbol '{sym_name}' not in target binary imports. "
                    if available:
                        hint += f"Available builtins: arm_logf. "
                        hint += f"Imported: {', '.join(available[2:5])}..."
                    raise RuntimeError(hint)
                src_va = text_va + rel_off
                _patch_branch26(text_buf, rel_off, src_va, stub)
            elif sym_section == "__text":
                # Local function call within the plugin — patch to cave address
                src_va = text_va + rel_off
                dst_va = text_va + sym_value
                _patch_branch26(text_buf, rel_off, src_va, dst_va)

        elif int(rel_type) == 3:  # ARM64_RELOC_PAGE21
            if sym_name is None or sym_section is None:
                continue
            if sym_section == "__text":
                target_va = text_va + sym_value
            elif sym_section in section_offsets:
                target_va = data_va + section_offsets[sym_section] + sym_value
            else:
                continue
            pc_va = text_va + rel_off
            _patch_page21(text_buf, rel_off, pc_va, target_va)

        elif int(rel_type) == 4:  # ARM64_RELOC_PAGEOFF12
            if sym_name is None or sym_section is None:
                continue
            if sym_section == "__text":
                target_va = text_va + sym_value
            elif sym_section in section_offsets:
                target_va = data_va + section_offsets[sym_section] + sym_value
            else:
                continue
            _patch_pageoff12(text_buf, rel_off, target_va)

    return bytes(text_buf), bytes(extra_buf)


