from __future__ import annotations

import subprocess
from pathlib import Path

import lief


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


LR_SAVE = b"\xfd\x7b\xbf\xa9"       # stp x29, x30, [sp, #-16]!
LR_RESTORE = b"\xfd\x7b\xc1\xa8"    # ldp x29, x30, [sp], #16


def build_hook_cave(
    cave_va: int,
    hook_va: int,
    hook_size: int,
    original_bytes: bytes,
    plugin_blobs: list[bytes],
) -> bytes:
    if not original_bytes or len(original_bytes) % 4:
        raise ValueError("hook window must be a non-empty multiple of 4 bytes")
    if hook_size < len(original_bytes) or hook_size % 4:
        raise ValueError("hook size must cover the preserved bytes and be 4-byte aligned")

    n_plugins = len(plugin_blobs)
    # layout: stp | BL*N | ldp | original | B
    control_size = 4 + 4 * n_plugins + 4 + len(original_bytes) + 4
    plugin_offsets: list[int] = []
    cursor = control_size
    for blob in plugin_blobs:
        cursor = (cursor + 3) & ~3
        plugin_offsets.append(cursor)
        cursor += (len(blob) + 3) & ~3

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

    # original instruction
    out += original_bytes

    # B back to hook_va + hook_size
    b_back_src = cave_va + 4 + 4 * n_plugins + 4 + len(original_bytes)
    out += encode_b(b_back_src, hook_va + hook_size).to_bytes(4, "little")

    # plugin blobs
    for blob in plugin_blobs:
        out += blob
        pad = (-len(blob)) % 4
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

    n_plugins = len(plugin_blobs)
    control_size = 4 + 4 * n_plugins + 4 + len(original_bytes) + 4
    payload_size = sum((len(b) + 3) & ~3 for b in plugin_blobs)
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
    cave_blob = build_hook_cave(cave_va, new_hook_va, hook_size, original_bytes, plugin_blobs)

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
