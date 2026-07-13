from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import lief


def align(value: int, page_size: int = 0x1000) -> int:
    return (value + page_size - 1) & ~(page_size - 1)


def seg_name(binary, name: str) -> str:
    if isinstance(binary, lief.MachO.Binary):
        return name if name.startswith("__") else f"__{name[:14]}"
    return name if name.startswith(".") else f".{name}"


def seg_va(binary, name: str, size: int) -> int:
    name = seg_name(binary, name)
    if isinstance(binary, lief.MachO.Binary):
        found = next((s for s in binary.segments if s.name == name), None)
        if found:
            return found.virtual_address
        last = binary.segments[-1]
        return align(last.virtual_address + last.virtual_size)
    found = binary.get_section(name)
    if found:
        return found.virtual_address
    last = binary.segments[-1]
    return align(last.virtual_address + last.virtual_size, getattr(binary, "page_size", 0x1000) or 0x1000)


@dataclass
class SegmentPlan:
    name: str
    size: int
    content: bytes = b""


def add_segment(binary_path: Path, plan: SegmentPlan, output_path: Path) -> None:
    binary = lief.parse(str(binary_path))
    if binary is None:
        raise RuntimeError(f"failed to parse {binary_path}")
    if isinstance(binary, lief.MachO.Binary):
        _add_macho(binary, plan)
    else:
        _add_elf(binary, plan)
    binary.write(str(output_path))


def write_at_offset(path: Path, offset: int, payload: bytes, size: int | None = None) -> None:
    data = bytearray(path.read_bytes())
    limit = len(payload) if size is None else size
    if offset + limit > len(data):
        raise ValueError(f"write out of range: 0x{offset:x}")
    data[offset:offset + len(payload)] = payload
    if size is not None and len(payload) < size:
        data[offset + len(payload):offset + size] = b"\x00" * (size - len(payload))
    path.write_bytes(data)


def segment_file_offset(binary, name: str) -> int:
    name = seg_name(binary, name)
    if isinstance(binary, lief.MachO.Binary):
        segment = next((s for s in binary.segments if s.name == name), None)
        if segment is None:
            raise ValueError(f"segment not found: {name}")
        section = next((s for s in binary.sections if s.segment_name == name), None)
        return section.offset if section is not None else segment.file_offset
    section = binary.get_section(name)
    if section is None:
        raise ValueError(f"section not found: {name}")
    return section.offset


def remap_macho_offset_va(before, after, file_offset: int) -> int:
    if not isinstance(before, lief.MachO.Binary) or not isinstance(after, lief.MachO.Binary):
        raise RuntimeError("failed to remap Mach-O address")
    for section in before.sections:
        start = section.offset
        end = section.offset + section.size
        if start <= file_offset < end:
            rel = file_offset - start
            found = next((s for s in after.sections if s.name == section.name and s.segment_name == section.segment_name), None)
            if found is None:
                break
            return found.virtual_address + rel
    va = before.offset_to_virtual_address(file_offset)
    if not isinstance(va, int):
        raise ValueError(f"cannot map original file offset 0x{file_offset:x}")
    return va


def _add_macho(binary, plan: SegmentPlan) -> None:
    name = seg_name(binary, plan.name)
    existing = next((s for s in binary.segments if s.name == name), None)
    content = plan.content[:plan.size].ljust(plan.size, b"\x00")
    if existing:
        existing.max_protection = 7
        existing.init_protection = 5
        existing.virtual_size = max(existing.virtual_size, align(plan.size))
        if existing.sections:
            existing.sections[0].content = memoryview(bytearray(content))
        else:
            existing.content = memoryview(bytearray(content))
        return
    last = binary.segments[-1]
    segment = lief.MachO.SegmentCommand()
    segment.name = name
    segment.virtual_address = align(last.virtual_address + last.virtual_size)
    segment.virtual_size = align(plan.size)
    segment.file_offset = align(last.file_offset + last.file_size)
    segment.max_protection = 7
    segment.init_protection = 5
    section = lief.MachO.Section()
    section.name = name
    section.segment_name = name
    section.alignment = 0
    section.content = memoryview(bytearray(content))
    segment.add_section(section)
    binary.add(segment)


def _add_elf(binary, plan: SegmentPlan) -> None:
    page = getattr(binary, "page_size", 0x1000) or 0x1000
    size = align(plan.size, page)
    name = seg_name(binary, plan.name)
    content = plan.content[:plan.size].ljust(size, b"\x00")
    section = binary.get_section(name)
    if section:
        if section.size < size:
            binary.extend(section, size - section.size)
        section.content = memoryview(bytearray(content))
        return
    section = lief.ELF.Section(name)
    section.type = lief.ELF.Section.TYPE.PROGBITS
    section.flags = lief.ELF.Section.FLAGS.ALLOC | lief.ELF.Section.FLAGS.EXECINSTR
    section.alignment = page
    section.content = memoryview(bytearray(content))
    binary.add(section, True, lief.ELF.Binary.SEC_INSERT_POS.POST_SEGMENT)
