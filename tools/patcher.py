from __future__ import annotations

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

    control_size = 4 * len(plugin_blobs) + len(original_bytes) + 4
    plugin_offsets: list[int] = []
    cursor = control_size
    for blob in plugin_blobs:
        cursor = (cursor + 3) & ~3
        plugin_offsets.append(cursor)
        cursor += (len(blob) + 3) & ~3

    out = bytearray()
    for idx, target_off in enumerate(plugin_offsets):
        src = cave_va + 4 * idx
        dst = cave_va + target_off
        out += encode_bl(src, dst).to_bytes(4, "little")

    out += original_bytes
    out += encode_b(
        cave_va + 4 * len(plugin_blobs) + len(original_bytes),
        hook_va + hook_size,
    ).to_bytes(4, "little")

    for blob in plugin_blobs:
        out += blob
        pad = (-len(blob)) % 4
        if pad:
            out += b"\x00" * pad

    return bytes(out)


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
