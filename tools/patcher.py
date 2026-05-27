from __future__ import annotations

import subprocess
from functools import cache
from pathlib import Path

import lief

from .compiler import extract_cave_asm


def encode_b_cond(imm19: int, cond: int) -> bytes:
    """Encode B.cond with signed 19-bit offset (in instructions, *4 for bytes)."""
    if imm19 < 0:
        imm19 += 0x80000
    insn = 0x54000000 | ((imm19 & 0x7FFFF) << 5) | (cond & 0xF)
    return insn.to_bytes(4, "little")


def encode_cmp_zr(reg: int, is_64bit: bool = True) -> bytes:
    """CMP Rn, #0  →  SUBS XZR/WZR, Rn, XZR/WZR."""
    if is_64bit:
        insn = 0xEB8F801F | (reg << 5)
    else:
        insn = 0x6B8F801F | (reg << 5)
    return insn.to_bytes(4, "little")


def _patch_original_insns(original_bytes: bytes, hook_va: int, cave_original_va: int) -> bytes:
    """Replace PC-relative branch instructions so they work from the cave."""
    result = bytearray()
    cave_cursor = cave_original_va
    NBIT = 1 << 31

    for i in range(0, len(original_bytes), 4):
        insn = int.from_bytes(original_bytes[i:i+4], 'little')
        orig_insn_va = hook_va + i

        # ── B (unconditional) ──
        if (insn & 0xFC000000) == 0x14000000:
            imm26 = insn & 0x3FFFFFF
            if imm26 & 0x2000000:
                imm26 -= 0x4000000
            target = orig_insn_va + imm26 * 4
            result += encode_b(cave_cursor, target).to_bytes(4, "little")
            cave_cursor += 4

        # ── BL (branch with link) ──
        elif (insn & 0xFC000000) == 0x94000000:
            imm26 = insn & 0x3FFFFFF
            if imm26 & 0x2000000:
                imm26 -= 0x4000000
            target = orig_insn_va + imm26 * 4
            result += encode_bl(cave_cursor, target).to_bytes(4, "little")
            cave_cursor += 4

        # ── CBZ / CBNZ ──
        elif (insn & 0x7E000000) == 0x34000000:
            sf = (insn >> 31) & 1
            rt = insn & 0x1F
            imm19 = (insn >> 5) & 0x7FFFF
            if imm19 & 0x40000:
                imm19 -= 0x80000
            target = orig_insn_va + imm19 * 4
            is_cbnz = (insn >> 24) & 1
            # Invert: for CBZ (taken if Z=1) skip B if NE; for CBNZ skip if EQ
            skip_cond = 0 if is_cbnz else 1  # EQ for CBNZ, NE for CBZ
            result += encode_cmp_zr(rt, bool(sf))
            result += encode_b_cond(2, skip_cond)        # skip 2 insns (past B)
            result += encode_b(cave_cursor + 8, target).to_bytes(4, "little")
            cave_cursor += 12

        # ── B.cond (conditional branch) ──
        elif (insn & 0xFF000010) == 0x54000000:
            cond = insn & 0xF
            imm19 = (insn >> 5) & 0x7FFFF
            if imm19 & 0x40000:
                imm19 -= 0x80000
            target = orig_insn_va + imm19 * 4
            inv_cond = cond ^ 1
            if inv_cond <= 13:
                result += encode_b_cond(1, inv_cond)     # skip 1 insn (B)
                result += encode_b(cave_cursor + 4, target).to_bytes(4, "little")
                cave_cursor += 8
            else:
                result += original_bytes[i:i+4]
                cave_cursor += 4

        # ── TBZ / TBNZ ──
        elif (insn & 0x7E000000) == 0x36000000:
            imm14 = (insn >> 19) & 0x3FFF
            if imm14 & 0x2000:
                imm14 -= 0x4000
            target = orig_insn_va + imm14 * 4
            rt = insn & 0x1F
            bit_pos = ((insn >> 26) & 0x1F) | ((insn >> 1) & 0x20)
            is_tbnz = (insn >> 24) & 1
            # Replace: TBZ Xt, #bit, label with:
            #   TST Xt, #(1 << bit)  (no direct encoding, use AND/UBFX instead)
            # Simpler: use LSL + TST approach or just skip
            # For now, just copy as-is (same limitation as original)
            result += original_bytes[i:i+4]
            cave_cursor += 4

        else:
            result += original_bytes[i:i+4]
            cave_cursor += 4

    return bytes(result)


def _patched_size(original_bytes: bytes) -> int:
    """Return the byte-size after _patch_original_insns expansion."""
    size = 0
    for i in range(0, len(original_bytes), 4):
        insn = int.from_bytes(original_bytes[i:i+4], 'little')
        if (insn & 0x7E000000) == 0x34000000:        # CBZ/CBNZ → 12 B
            size += 12
        elif (insn & 0xFF000010) == 0x54000000:      # B.cond  → 8 B
            size += 8
        else:                                          # no expansion
            size += 4
    return size


def encode_mov(rd: int, rm: int, is_64bit: bool = True) -> bytes:
    """Encode MOV Xd, Xm (ORR Xd, XZR, Xm) or MOV Wd, Wm (ORR Wd, WZR, Wm)."""
    if is_64bit:
        insn = 0xAA0003E0 | (rm << 16) | rd
    else:
        insn = 0x2A0003E0 | (rm << 16) | rd
    return insn.to_bytes(4, "little")


def _parse_reg_name(name: str) -> tuple[int, bool]:
    name = name.strip().lower()
    if name.startswith("x"):
        return int(name[1:]), True
    elif name.startswith("w"):
        return int(name[1:]), False
    raise ValueError(f"invalid register name: {name}")


def generate_param_wrapper(param_regs: list[str], wrapper_va: int, plugin_va: int) -> bytes:
    out = bytearray()
    for i, reg_name in enumerate(param_regs[:8]):
        reg_num, is_64bit = _parse_reg_name(reg_name)
        out += encode_mov(i, reg_num, is_64bit)
    b_off = len(out)
    out += encode_b(wrapper_va + b_off, plugin_va).to_bytes(4, "little")
    return bytes(out)


def encode_b(src_va: int, dst_va: int) -> int:
    delta = dst_va - src_va
    if delta % 4:
        raise ValueError("AArch64 branch target must be 4-byte aligned")
    imm26 = delta >> 2
    if not -(1 << 25) <= imm26 < (1 << 25):
        raise ValueError("branch target out of range for 26-bit B instruction")
    return 0x14000000 | (imm26 & 0x03FFFFFF)


def encode_bl(src_va: int, dst_va: int) -> int:
    delta = dst_va - src_va
    if delta % 4:
        raise ValueError("AArch64 branch target must be 4-byte aligned")
    imm26 = delta >> 2
    if not -(1 << 25) <= imm26 < (1 << 25):
        raise ValueError("branch target out of range for 26-bit BL instruction")
    return 0x94000000 | (imm26 & 0x03FFFFFF)


@cache
def _cave_asm():
    lr_save, lr_restore, ret = extract_cave_asm()
    return lr_save, lr_restore, ret

LR_SAVE, LR_RESTORE, RET = _cave_asm()


def build_hook_cave(
    cave_va: int,
    hook_va: int,
    hook_size: int,
    original_bytes: bytes,
    plugin_blobs: list,
    target_binary: Path | None = None,
    detour: bool = False,
) -> bytes:
    if not original_bytes or len(original_bytes) % 4:
        raise ValueError("hook window must be a non-empty multiple of 4 bytes")
    if hook_size < len(original_bytes) or hook_size % 4:
        raise ValueError("hook size must cover the preserved bytes and be 4-byte aligned")

    from .compiler import PluginBlob

    n_plugins = len(plugin_blobs)
    if detour:
        patched_original = original_bytes
        control_size = 4 + 4 * n_plugins + 4 + 4
    else:
        cave_original_va = cave_va + 4 + 4 * n_plugins + 4
        patched_original = _patch_original_insns(original_bytes, hook_va, cave_original_va)
        control_size = 4 + 4 * n_plugins + 4 + len(patched_original) + 4

    # Compute plugin offsets and build resolved blobs
    plugin_offsets: list[int] = []
    resolved_blobs: list[bytes] = []
    cursor = control_size
    for blob in plugin_blobs:
        cursor = (cursor + 3) & ~3

        register_args = blob.register_args if isinstance(blob, PluginBlob) else None

        if register_args:
            wrapper_size = 4 * len(register_args) + 4
            wrapper_off = cursor
            cursor += wrapper_size
            cursor = (cursor + 3) & ~3
            plugin_off = cursor

            plugin_offsets.append(wrapper_off)

            text_va = cave_va + plugin_off
            aligned_text_size = (len(blob.text) + 15) & ~15
            data_va = text_va + aligned_text_size
            built = blob.build(text_va, data_va, target_binary)

            wrapper = generate_param_wrapper(
                register_args, cave_va + wrapper_off, cave_va + plugin_off,
            )
            resolved_blobs.append(wrapper)
            resolved_blobs.append(built)
            cursor = plugin_off + len(built)
        else:
            plugin_offsets.append(cursor)
            if isinstance(blob, PluginBlob):
                text_va = cave_va + cursor
                aligned_text_size = (len(blob.text) + 15) & ~15
                data_va = text_va + aligned_text_size
                built = blob.build(text_va, data_va, target_binary)
                resolved_blobs.append(built)
                cursor += len(built)
            else:
                resolved_blobs.append(blob)
                cursor += len(blob)

        cursor = (cursor + 3) & ~3

    out = bytearray()

    # save LR / FP
    out += LR_SAVE

    # BL to each plugin
    for idx, target_off in enumerate(plugin_offsets):
        src = cave_va + 4 + 4 * idx
        dst = cave_va + target_off
        out += encode_bl(src, dst).to_bytes(4, "little")

    # restore LR / FP
    out += LR_RESTORE

    if detour:
        out += RET
    else:
        out += patched_original

        b_back_src = cave_va + 4 + 4 * n_plugins + 4 + len(patched_original)
        out += encode_b(b_back_src, hook_va + hook_size).to_bytes(4, "little")

    # plugin blobs
    for blob_bytes in resolved_blobs:
        out += blob_bytes
        pad = (-len(blob_bytes)) % 4
        if pad:
            out += b"\x00" * pad

    return bytes(out)


def _page_align(value: int, page: int = 0x4000) -> int:
    return (value + page - 1) & ~(page - 1)


def _macho_seg_name(name: str) -> str:
    return name if name.startswith("__") else f"__{name}"


def _codesign(path: Path) -> None:
    subprocess.run(
        ["codesign", "--force", "--sign", "-", str(path)],
        capture_output=True,
    )


def _macho_add_segment(
    binary: lief.MachO.Binary,
    seg_name: str,
    content: bytes,
    maxprot: int = 5,
    initprot: int = 5,
) -> None:
    """Add a named segment (with same-named section) to a LIEF binary.

    LIEF handles Mach-O structure, page alignment, and load-command
    bookkeeping.
    """
    existing = next((s for s in binary.segments if s.name == seg_name), None)
    if existing is not None:
        existing.max_protection = maxprot
        existing.init_protection = initprot
        if existing.virtual_size < _page_align(len(content)):
            existing.virtual_size = _page_align(len(content))
        existing.sections[0].content = memoryview(bytearray(content))
        return

    last_seg = binary.segments[-1]
    seg_va = (last_seg.virtual_address + last_seg.virtual_size + 0xFFF) & ~0xFFF
    seg_virtual_size = _page_align(len(content))

    segment = lief.MachO.SegmentCommand()
    segment.name = seg_name
    segment.virtual_address = seg_va
    segment.virtual_size = seg_virtual_size
    segment.file_offset = (
        last_seg.file_offset + last_seg.file_size + 0xFFF
    ) & ~0xFFF
    segment.max_protection = maxprot
    segment.init_protection = initprot

    sec = lief.MachO.Section()
    sec.name = seg_name
    sec.segment_name = seg_name
    sec.alignment = 0
    sec.content = memoryview(bytearray(content))
    segment.add_section(sec)

    binary.add(segment)


def patch_hook_macho(
    binary_path: Path,
    output_path: Path,
    hook_file_off: int,
    hook_size: int,
    original_bytes: bytes,
    plugin_blobs: list[bytes],
    seg_name: str,
    detour: bool = False,
) -> tuple[int, int]:
    """Mach-O: add a named segment via LIEF, place the hook cave in it,
    and write the B instruction at the hook point.

    Uses two LIEF passes: pass 1 adds the segment so we learn the real
    VAs LIEF assigns; pass 2 updates section content with the correctly-
    addressed cave blob.
    """
    # ── parse original binary to get base VA info ──
    orig_binary = lief.parse(str(binary_path))
    if orig_binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    text_seg = next((s for s in orig_binary.segments if s.name == "__TEXT"), None)
    if text_seg is None:
        raise ValueError("no __TEXT segment")
    orig_hook_va = text_seg.virtual_address + (hook_file_off - text_seg.file_offset)

    seg_name = _macho_seg_name(seg_name)

    # ── pass 1: add segment with dummy content, write via LIEF ──
    macho = lief.MachO.parse(str(binary_path))
    binary = macho[0]

    from .compiler import PluginBlob

    n_plugins = len(plugin_blobs)
    if detour:
        control_size = 4 + 4 * n_plugins + 4 + 4
    else:
        control_size = 4 + 4 * n_plugins + 4 + _patched_size(original_bytes) + 4
    payload_size = sum(
        (b.total_bytes if isinstance(b, PluginBlob) else len(b)) for b in plugin_blobs
    )
    # Add wrapper overhead for register-args plugins
    for b in plugin_blobs:
        if isinstance(b, PluginBlob) and b.register_args:
            payload_size += 4 * len(b.register_args) + 4
    cave_size = control_size + payload_size

    _macho_add_segment(binary, seg_name, b"\x00" * cave_size)
    macho.write(str(output_path))

    # ── re-parse to learn LIEF's actual layout ──
    new_binary = lief.parse(str(output_path))
    if new_binary is None:
        raise RuntimeError(f"failed to re-parse {output_path}")

    new_text_section = next(
        s for s in new_binary.sections
        if hasattr(s, "segment_name") and s.segment_name == "__TEXT" and s.name == "__text"
    )
    orig_text_section = next(
        s for s in orig_binary.sections
        if hasattr(s, "segment_name") and s.segment_name == "__TEXT" and s.name == "__text"
    )

    # LIEF may shift section VAs when extending __TEXT
    va_delta = new_text_section.virtual_address - orig_text_section.virtual_address
    new_hook_va = orig_hook_va + va_delta

    # get the actual cave VA from the new segment
    new_seg = next((s for s in new_binary.segments if s.name == seg_name), None)
    if new_seg is None:
        raise RuntimeError(f"segment {seg_name} not found after LIEF pass 1")
    cave_va = new_seg.virtual_address

    # ── build the real cave blob with correct VAs ──
    cave_blob = build_hook_cave(cave_va, new_hook_va, hook_size, original_bytes, plugin_blobs, target_binary=binary_path, detour=detour)

    # ── pass 2: update section content ──
    macho2 = lief.MachO.parse(str(output_path))
    binary2 = macho2[0]
    seg2 = next(s for s in binary2.segments if s.name == seg_name)
    sec2 = seg2.sections[0]
    sec2.content = memoryview(bytearray(cave_blob))
    macho2.write(str(output_path))

    # ── write B instruction at the new hook point ──
    final_binary = lief.parse(str(output_path))
    new_hook_off = final_binary.virtual_address_to_offset(new_hook_va)
    if not isinstance(new_hook_off, int) or new_hook_off < 0:
        raise ValueError(f"failed to map hook VA 0x{new_hook_va:x} to file offset")

    hook_b = encode_b(new_hook_va, cave_va)
    data = bytearray(output_path.read_bytes())
    data[new_hook_off : new_hook_off + 4] = hook_b.to_bytes(4, "little")
    for pad in range(4, hook_size, 4):
        data[new_hook_off + pad : new_hook_off + pad + 4] = b"\x1f\x20\x03\xd5"
    output_path.write_bytes(data)

    _codesign(output_path)

    return new_hook_va, cave_va


def patch_hook_window(binary_path: Path, output_path: Path, src_va: int, size: int, dst_va: int) -> None:
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    if size % 4:
        raise ValueError("hook size must be a multiple of 4")
    off = binary.virtual_address_to_offset(src_va)
    if off < 0:
        raise ValueError(f"failed to map virtual address 0x{src_va:x} to file offset")
    data = bytearray(Path(binary_path).read_bytes())
    data[off : off + 4] = encode_b(src_va, dst_va).to_bytes(4, "little")
    for pad in range(4, size, 4):
        data[off + pad : off + pad + 4] = b"\x1f\x20\x03\xd5"
    Path(output_path).write_bytes(data)
