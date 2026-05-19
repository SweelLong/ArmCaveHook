from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import lief


def align(value: int, page_size: int = 0x1000) -> int:
    return (value + page_size - 1) & ~(page_size - 1)


def seg_name(binary, name: str) -> str:
    if isinstance(binary, lief.MachO.Binary):
        return name if name.startswith("__") else f"__{name}"
    return name if name.startswith(".") else f".{name}"


def seg_va(binary, name: str, size: int) -> int:
    name = seg_name(binary, name)
    if isinstance(binary, lief.MachO.Binary):
        found = next((s for s in binary.segments if s.name == name), None)
        if found is not None:
            return found.virtual_address
        last = binary.segments[-1]
        return align(last.virtual_address + last.virtual_size)
    found = binary.get_section(name)
    if found is not None:
        return found.virtual_address
    last = binary.segments[-1]
    page_size = getattr(binary, "page_size", 0x1000) or 0x1000
    return align(last.virtual_address + last.virtual_size, page_size)


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
        _add_segment_macho(binary, plan)
    else:
        _add_segment_elf(binary, plan)
    binary.write(str(output_path))


def _add_segment_macho(binary, plan: SegmentPlan) -> None:
    name = seg_name(binary, plan.name)
    last = binary.segments[-1]
    seg_va_val = align(last.virtual_address + last.virtual_size)
    seg_file = align(last.file_offset + last.file_size)
    seg_virtual_size = align(plan.size)

    existing = next((s for s in binary.segments if s.name == name), None)
    if existing is not None:
        if existing.virtual_size < existing.file_size:
            existing.virtual_size = align(existing.file_size)
        existing.max_protection = 7
        existing.init_protection = 5
        if plan.content:
            pld = plan.content[:plan.size]
            # write content into the segment's section if present
            for sec in existing.sections:
                if sec.content:
                    sec.content = memoryview(bytearray(pld.ljust(existing.file_size, b"\x00")))
                    break
            else:
                existing.content = memoryview(bytearray(pld.ljust(existing.file_size, b"\x00")))
        return

    segment = lief.MachO.SegmentCommand()
    segment.name = name
    segment.virtual_address = seg_va_val
    segment.virtual_size = seg_virtual_size
    segment.file_offset = seg_file
    segment.max_protection = 7
    segment.init_protection = 5

    pld = plan.content[:plan.size].ljust(plan.size, b"\x00")
    sec = lief.MachO.Section()
    sec.name = name
    sec.segment_name = name
    sec.alignment = 0
    sec.content = memoryview(bytearray(pld))
    segment.add_section(sec)

    binary.add(segment)


def _add_segment_elf(binary, plan: SegmentPlan) -> None:
    page_size = getattr(binary, "page_size", 0x1000) or 0x1000
    size = align(plan.size, page_size)
    name = seg_name(binary, plan.name)

    for segment in list(binary.segments):
        if (
            segment.type == lief.ELF.Segment.TYPE.LOAD
            and len(list(segment.sections)) == 0
            and segment.physical_size == size
            and segment.virtual_size == size
            and segment.has(lief.ELF.Segment.FLAGS.R)
            and segment.has(lief.ELF.Segment.FLAGS.W)
            and segment.has(lief.ELF.Segment.FLAGS.X)
        ):
            binary.remove(segment, True)

    section = binary.get_section(name)
    pld = plan.content[:plan.size].ljust(size, b"\x00")
    if section is not None:
        if section.size < size:
            binary.extend(section, size - section.size)
        section.content = memoryview(bytearray(pld))
        return

    section = lief.ELF.Section(name)
    section.type = lief.ELF.Section.TYPE.PROGBITS
    section.flags = lief.ELF.Section.FLAGS.ALLOC | lief.ELF.Section.FLAGS.EXECINSTR
    section.alignment = page_size
    section.content = memoryview(bytearray(pld))
    binary.add(section, True, lief.ELF.Binary.SEC_INSERT_POS.POST_SEGMENT)
