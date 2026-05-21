import os
import sys
import lief
import shutil

def align_page(value: int, page_size: int = 0x1000) -> int:
    return (value + page_size - 1) & ~(page_size - 1)

def add_segment_macho(input_path: str, output_path: str, seg_name: str, seg_size: int, seg_maxprot: int, seg_initprot: int):
    macho = lief.MachO.parse(input_path)
    for idx, binary in enumerate(macho):
        ft = binary.header.file_type
        print(f"[*] Mach-O slice {idx}: {ft}")
        existing = next((s for s in binary.segments if s.name == seg_name), None)
        if existing is not None:
            existing.max_protection = seg_maxprot
            existing.init_protection = seg_initprot
            if existing.virtual_size < existing.file_size:
                old_vsz = existing.virtual_size
                existing.virtual_size = align_page(existing.file_size)
                print(
                    f"[+] Fixed '{seg_name}' virtual_size: "
                    f"0x{old_vsz:x} -> 0x{existing.virtual_size:x}"
                )
            print(
                f"[+] Updated segment '{seg_name}' protection: "
                f"max={seg_maxprot} init={seg_initprot}"
            )
            continue
        last_seg = binary.segments[-1]
        seg_va = (last_seg.virtual_address + last_seg.virtual_size + 0xFFF) & ~0xFFF
        seg_file_off = (last_seg.file_offset + last_seg.file_size + 0xFFF) & ~0xFFF
        seg_virtual_size = align_page(seg_size)
        segment = lief.MachO.SegmentCommand()
        segment.name = seg_name
        segment.virtual_address = seg_va
        segment.virtual_size = seg_virtual_size
        segment.file_offset = seg_file_off
        # segment.file_size = seg_size
        segment.max_protection = seg_maxprot
        segment.init_protection = seg_initprot
        sec = lief.MachO.Section()
        sec.name = seg_name
        sec.segment_name = seg_name
        sec.alignment = 0
        sec.content = memoryview(bytearray(seg_size))
        segment.add_section(sec)
        binary.add(segment)
        print(
            f"[+] Added macho segment '{seg_name}' at va=0x{seg_va:x} file=0x{seg_file_off:x} "
            f"max={seg_maxprot} init={seg_initprot} "
            f"vsz=0x{seg_virtual_size:x} fsz=0x{seg_size:x}"
        )
    print(f"[*] Writing to {output_path}")
    macho.write(output_path)
    print(f"[+] Done: {output_path}")

def add_segment_elf(input_path: str, output_path: str, seg_name: str, seg_size: int):
    elf = lief.ELF.parse(input_path)
    if elf is None:
        print("[!] Failed to parse ELF file")
        sys.exit(1)
    print(f"[*] ELF: {elf.header.identity_class}")
    page_size = elf.page_size
    seg_size = align_page(seg_size, page_size)
    for segment in list(elf.segments):
        if (
            segment.type == lief.ELF.Segment.TYPE.LOAD and
            len(list(segment.sections)) == 0 and
            segment.physical_size == seg_size and
            segment.virtual_size == seg_size and
            segment.has(lief.ELF.Segment.FLAGS.R) and
            segment.has(lief.ELF.Segment.FLAGS.W) and
            segment.has(lief.ELF.Segment.FLAGS.X)
        ):
            print(
                f"[!] Removing legacy anonymous RWX LOAD at "
                f"va=0x{segment.virtual_address:x} file=0x{segment.file_offset:x}"
            )
            elf.remove(segment, True)
    section = elf.get_section(seg_name)
    if section is not None:
        print(
            f"[!] Section '{seg_name}' already exists at "
            f"va=0x{section.virtual_address:x} file=0x{section.offset:x}"
        )
        if section.size < seg_size:
            old_size = section.size
            elf.extend(section, seg_size - section.size)
            print(f"[+] Extended section '{seg_name}': 0x{old_size:x} -> 0x{seg_size:x}")
    else:
        section = lief.ELF.Section(seg_name)
        section.type = lief.ELF.Section.TYPE.PROGBITS
        section.flags = (
            lief.ELF.Section.FLAGS.ALLOC |
            lief.ELF.Section.FLAGS.EXECINSTR
        )
        section.alignment = page_size
        section.content = memoryview(bytearray(seg_size))
        section = elf.add(section, True, lief.ELF.Binary.SEC_INSERT_POS.POST_SEGMENT)
        print(
            f"[+] Added LOAD section '{seg_name}' at "
            f"va=0x{section.virtual_address:x} file=0x{section.offset:x} "
            f"size=0x{section.size:x}"
        )
        for segment in section.segments:
            print(f"    segment flags={segment.flags} align=0x{segment.alignment:x}")
    print(f"[*] Writing to {output_path}")
    elf.write(output_path)
    print(f"[+] Done: {output_path}")

if __name__ == "__main__":
    file = "./libcocos2dcpp.so" # 输入文件路径
    size = 0x1000 # 段大小
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    if not os.path.exists(file + ".bak"):
        shutil.copyfile(file, file + ".bak")
    macho = lief.MachO.parse(file)
    if macho is not None and len(list(macho)) > 0:
        add_segment_macho(file, file, "__nulcorepivot", size, 7, 5)
    else:
        add_segment_elf(file, file, ".nulcorepivot", size)
