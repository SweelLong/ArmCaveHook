from __future__ import annotations

import subprocess
from functools import cache
from pathlib import Path

import lief

from .compiler import extract_cave_asm

NOP = b"\x1f\x20\x03\xd5"
DST = b"\xbe\xba\xfe\xca"
NEXT = b"\xfe\xca\xad\xde"
CONV = b"\xfe\xca\xef\xbe"


def encode_b(src_va: int, dst_va: int) -> int:
    delta = dst_va - src_va
    if delta % 4:
        raise ValueError("branch target must be 4-byte aligned")
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError("branch target out of range")
    return 0x14000000 | (imm & 0x03FFFFFF)


def encode_bl(src_va: int, dst_va: int) -> int:
    delta = dst_va - src_va
    if delta % 4:
        raise ValueError("branch target must be 4-byte aligned")
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError("branch target out of range")
    return 0x94000000 | (imm & 0x03FFFFFF)


def _branch(src_va: int, dst_va: int, scratch: int = 16) -> bytes:
    try:
        return encode_b(src_va, dst_va).to_bytes(4, "little")
    except ValueError:
        src_page = src_va & ~0xFFF
        dst_page = dst_va & ~0xFFF
        imm21 = ((dst_page - src_page) >> 12) & 0x1FFFFF
        adrp = 0x90000000 | scratch | ((imm21 & 3) << 29) | (((imm21 >> 2) & 0x7FFFF) << 5)
        add = 0x91000000 | scratch | (scratch << 5) | ((dst_va & 0xFFF) << 10)
        br = 0xD61F0000 | (scratch << 5)
        return adrp.to_bytes(4, "little") + add.to_bytes(4, "little") + br.to_bytes(4, "little")


def _target(insn: int, va: int) -> int | None:
    if (insn & 0xFC000000) in (0x14000000, 0x94000000):
        imm = insn & 0x3FFFFFF
        if imm & 0x2000000:
            imm -= 0x4000000
        return va + imm * 4
    if (insn & 0xFF000010) == 0x54000000 or (insn & 0x7E000000) == 0x34000000:
        imm = (insn >> 5) & 0x7FFFF
        if imm & 0x40000:
            imm -= 0x80000
        return va + imm * 4
    if (insn & 0x7E000000) == 0x36000000:
        imm = (insn >> 19) & 0x3FFF
        if imm & 0x2000:
            imm -= 0x4000
        return va + imm * 4
    return None


def _patched(original: bytes, hook_va: int, cave_va: int) -> bytes:
    out = bytearray()
    cur = cave_va
    for i in range(0, len(original), 4):
        raw = original[i:i + 4]
        insn = int.from_bytes(raw, "little")
        va = hook_va + i
        if (insn & 0xFC000000) == 0x14000000:
            b = _branch(cur, _target(insn, va))
            out += b
            cur += len(b)
        elif (insn & 0xFC000000) == 0x94000000:
            out += encode_bl(cur, _target(insn, va)).to_bytes(4, "little")
            cur += 4
        else:
            out += raw
            cur += 4
    return bytes(out)


def _patched_size(original: bytes) -> int:
    return len(_patched(original, 0, 0))


def _reg(name: str) -> tuple[int, bool]:
    name = name.lower().strip()
    return int(name[1:]), name.startswith("x")


def _mov(rd: int, rm: int, is64: bool) -> bytes:
    insn = (0xAA0003E0 if is64 else 0x2A0003E0) | (rm << 16) | rd
    return insn.to_bytes(4, "little")


def _wrapper(regs: list[str], wrapper_va: int, plugin_va: int) -> bytes:
    out = bytearray()
    for i, r in enumerate(regs[:8]):
        n, is64 = _reg(r)
        out += _mov(i, n, is64)
    out += encode_b(wrapper_va + len(out), plugin_va).to_bytes(4, "little")
    return bytes(out)


@cache
def _frame() -> tuple[bytes, bytes, bytes]:
    return extract_cave_asm()


def build_hook_cave(cave_va: int, hook_va: int, hook_size: int, original: bytes, plugin_blobs: list, target_binary: Path | None = None, detour: bool = False, branch_host: bool = False, nop_addrs: list[int] | None = None) -> bytes:
    if not original or len(original) % 4 or hook_size % 4:
        raise ValueError("invalid hook window")
    from .compiler import PluginBlob
    save, restore, ret = _frame()
    n = len(plugin_blobs)
    target_dst = None
    if branch_host:
        detour = True
        for i in range(0, len(original), 4):
            target_dst = _target(int.from_bytes(original[i:i + 4], "little"), hook_va + i)
            if target_dst is not None:
                break
        if target_dst is None:
            raise ValueError("branch hook needs a branch instruction")
    patched = original if detour else _patched(original, hook_va, cave_va + 4 + n * 4 + 4)
    control_size = 4 + n * 4 + 4 + (4 if detour else len(patched) + 4)
    offsets, chunks = [], []
    cur = control_size
    for blob in plugin_blobs:
        cur = (cur + 3) & ~3
        regs = blob.register_args if isinstance(blob, PluginBlob) else None
        entry = blob.entry_offset if isinstance(blob, PluginBlob) else 0
        if regs:
            woff = cur
            cur += 4 * len(regs) + 4
            cur = (cur + 3) & ~3
            poff = cur
            offsets.append(woff)
            built = blob.build(cave_va + poff, cave_va + poff + ((len(blob.text) + 15) & ~15), target_binary)
            chunks.append(_wrapper(regs, cave_va + woff, cave_va + poff + entry))
            chunks.append(built)
            cur = poff + len(built)
        else:
            offsets.append(cur + entry)
            if isinstance(blob, PluginBlob):
                built = blob.build(cave_va + cur, cave_va + cur + ((len(blob.text) + 15) & ~15), target_binary)
            else:
                built = blob
            chunks.append(built)
            cur += len(built)
    out = bytearray(save)
    for i, off in enumerate(offsets):
        out += encode_bl(cave_va + 4 + i * 4, cave_va + off).to_bytes(4, "little")
    out += restore
    out += ret if detour else patched + _branch(cave_va + 4 + n * 4 + 4 + len(patched), hook_va + hook_size)
    for chunk in chunks:
        out += chunk
        out += b"\x00" * ((-len(out)) % 4)
    if target_dst is not None:
        for marker, target in ((DST, target_dst), (NEXT, hook_va + 4), (CONV, hook_va + hook_size)):
            idx = 0
            while True:
                idx = out.find(marker, idx)
                if idx < 0:
                    break
                out[idx:idx + 4] = encode_b(cave_va + idx, target).to_bytes(4, "little")
                idx += 4
    return bytes(out)


def _codesign(path: Path) -> None:
    subprocess.run(["codesign", "--force", "--sign", "-", str(path)], capture_output=True)


def patch_hook_window(binary_path: Path, output_path: Path, src_va: int, size: int, dst_va: int) -> None:
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    off = binary.virtual_address_to_offset(src_va)
    if not isinstance(off, int) or off < 0:
        raise ValueError(f"cannot map VA 0x{src_va:x}")
    hook = _branch(src_va, dst_va)
    if len(hook) > size:
        raise ValueError(f"hook window 0x{size:x} too small")
    data = bytearray(Path(output_path).read_bytes())
    data[off:off + len(hook)] = hook
    for pos in range(len(hook), size, 4):
        data[off + pos:off + pos + 4] = NOP
    Path(output_path).write_bytes(data)


def patch_bytes_va(binary_path: Path, output_path: Path, va: int, payload: bytes) -> None:
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    off = binary.virtual_address_to_offset(va)
    if not isinstance(off, int) or off < 0:
        raise ValueError(f"cannot map VA 0x{va:x}")
    data = bytearray(Path(output_path).read_bytes())
    if off + len(payload) > len(data):
        raise ValueError(f"patch out of range: 0x{va:x}")
    data[off:off + len(payload)] = payload
    Path(output_path).write_bytes(data)


def patch_hook_macho(binary_path: Path, output_path: Path, hook_file_off: int, hook_size: int, original: bytes, plugin_blobs: list, seg_name: str, detour: bool = False, branch_host: bool = False, nop_addrs: list[int] | None = None) -> tuple[int, int]:
    from .segment import SegmentPlan, add_segment, remap_macho_offset_va, seg_va, segment_file_offset, write_at_offset
    binary = lief.parse(str(binary_path))
    if binary is None or not isinstance(binary, lief.MachO.Binary):
        raise RuntimeError(f"failed to parse {binary_path}")
    control = 4 + len(plugin_blobs) * 4 + 4 + (4 if detour else _patched_size(original) + 4)
    size = control
    for b in plugin_blobs:
        size += b.total_bytes if hasattr(b, "total_bytes") else len(b)
        if getattr(b, "register_args", None):
            size += 4 * len(b.register_args) + 4
    size = max(size, 4)
    add_segment(output_path, SegmentPlan(seg_name, size, b"\x00" * size), output_path)
    patched_binary = lief.parse(str(output_path))
    if patched_binary is None or not isinstance(patched_binary, lief.MachO.Binary):
        raise RuntimeError(f"failed to parse {output_path}")
    hook_va = remap_macho_offset_va(binary, patched_binary, hook_file_off)
    cave_va = seg_va(patched_binary, seg_name, size)
    cave_off = segment_file_offset(patched_binary, seg_name)
    blob = build_hook_cave(cave_va, hook_va, hook_size, original, plugin_blobs, output_path, detour, branch_host, nop_addrs)
    write_at_offset(output_path, cave_off, blob, max(size, 4))
    patch_hook_window(output_path, output_path, hook_va, hook_size, cave_va)
    _codesign(output_path)
    return hook_va, cave_va
