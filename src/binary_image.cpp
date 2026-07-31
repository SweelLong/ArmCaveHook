#include "binary_image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {

template <typename T>
T read_le(const std::vector<uint8_t> &data, size_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("truncated binary structure");
    using U = typename std::make_unsigned<T>::type;
    U value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= (U)data[offset + i] << (i * 8);
    return static_cast<T>(value);
}

template <typename T>
T read_be(const std::vector<uint8_t> &data, size_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("truncated binary structure");
    using U = typename std::make_unsigned<T>::type;
    U value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value = (U)((value << 8) | data[offset + i]);
    return static_cast<T>(value);
}

template <typename T>
void write_le(std::vector<uint8_t> &data, size_t offset, T value) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("binary write out of range");
    using U = typename std::make_unsigned<T>::type;
    U raw = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i)
        data[offset + i] = (uint8_t)(raw >> (i * 8));
}

template <typename T>
void write_be(std::vector<uint8_t> &data, size_t offset, T value) {
    if (offset > data.size() || sizeof(T) > data.size() - offset)
        throw std::runtime_error("binary write out of range");
    using U = typename std::make_unsigned<T>::type;
    U raw = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i)
        data[offset + sizeof(T) - 1 - i] = (uint8_t)(raw >> (i * 8));
}

uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

std::string fixed_name(const std::vector<uint8_t> &data, size_t offset, size_t size) {
    if (offset > data.size() || size > data.size() - offset)
        throw std::runtime_error("truncated binary name");
    size_t length = 0;
    while (length < size && data[offset + length] != 0) ++length;
    return std::string((const char *)data.data() + offset, length);
}

void put_fixed_name(std::vector<uint8_t> &data, size_t offset, size_t size,
                    const std::string &name) {
    if (offset > data.size() || size > data.size() - offset)
        throw std::runtime_error("binary name write out of range");
    std::fill(data.begin() + offset, data.begin() + offset + size, 0);
    memcpy(data.data() + offset, name.data(), std::min(size, name.size()));
}

std::vector<uint8_t> read_all(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot read: " + path.string());
    auto length = stream.tellg();
    if (length < 0) throw std::runtime_error("cannot size: " + path.string());
    stream.seekg(0);
    std::vector<uint8_t> data((size_t)length);
    if (!data.empty()) stream.read((char *)data.data(), length);
    return data;
}

std::string table_string(const std::vector<uint8_t> &data, uint64_t table_offset,
                         uint64_t table_size, uint32_t string_offset) {
    if (string_offset >= table_size || table_offset + string_offset >= data.size())
        return {};
    size_t start = (size_t)table_offset + string_offset;
    size_t limit = (size_t)std::min<uint64_t>(data.size(), table_offset + table_size);
    size_t end = start;
    while (end < limit && data[end] != 0) ++end;
    return std::string((const char *)data.data() + start, end - start);
}

}

bool BinarySymbol::undefined() const {
    return (type & 0x0e) == 0;
}

std::vector<uint8_t> BinarySection::content(const std::vector<uint8_t> &image) const {
    if (zero_fill) return std::vector<uint8_t>((size_t)size, 0);
    if (offset >= image.size()) return {};
    size_t length = (size_t)std::min<uint64_t>(size, image.size() - offset);
    return std::vector<uint8_t>(image.begin() + offset, image.begin() + offset + length);
}

std::unique_ptr<BinaryImage> BinaryImage::parse(const std::filesystem::path &path) {
    auto image = std::unique_ptr<BinaryImage>(new BinaryImage());
    auto input = read_all(path);
    if (input.size() < 4) return nullptr;

    uint32_t fat_magic = read_be<uint32_t>(input, 0);
    if (fat_magic == 0xcafebabe || fat_magic == 0xcafebabf) {
        image->fat_ = true;
        image->fat64_ = fat_magic == 0xcafebabf;
        uint32_t count = read_be<uint32_t>(input, 4);
        size_t entry_size = image->fat64_ ? 32 : 20;
        if (count > 128 || 8 + (uint64_t)count * entry_size > input.size())
            return nullptr;
        size_t selected = count ? count - 1 : 0;
        for (uint32_t i = 0; i < count; ++i) {
            size_t off = 8 + (size_t)i * entry_size;
            FatSlice slice;
            slice.cpu_type = read_be<int32_t>(input, off);
            slice.cpu_subtype = read_be<int32_t>(input, off + 4);
            uint64_t file_offset = image->fat64_ ? read_be<uint64_t>(input, off + 8)
                                                   : read_be<uint32_t>(input, off + 8);
            uint64_t file_size = image->fat64_ ? read_be<uint64_t>(input, off + 16)
                                                 : read_be<uint32_t>(input, off + 12);
            slice.alignment = read_be<uint32_t>(input, off + (image->fat64_ ? 24 : 16));
            if (image->fat64_) slice.reserved = read_be<uint32_t>(input, off + 28);
            if (file_offset > input.size() || file_size > input.size() - file_offset)
                return nullptr;
            slice.data.assign(input.begin() + file_offset, input.begin() + file_offset + file_size);
            if ((uint32_t)slice.cpu_type == 0x0100000c) selected = i;
            image->fat_slices_.push_back(std::move(slice));
        }
        if (image->fat_slices_.empty()) return nullptr;
        image->selected_slice_ = selected;
        image->data_ = image->fat_slices_[selected].data;
        return image->parse_macho() ? std::move(image) : nullptr;
    }

    image->data_ = std::move(input);
    if (image->parse_macho()) return image;
    image->sections_.clear();
    image->segments_.clear();
    image->symbols_.clear();
    if (image->parse_elf()) return image;
    return nullptr;
}

bool BinaryImage::parse_macho() {
    constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf;
    constexpr uint32_t LC_SEGMENT_64 = 0x19;
    constexpr uint32_t LC_SYMTAB = 0x2;
    constexpr uint32_t LC_DYSYMTAB = 0xb;
    constexpr uint32_t LC_MAIN = 0x80000028;
    constexpr uint32_t LC_DYLD_CHAINED_FIXUPS = 0x80000034;
    if (data_.size() < 32 || read_le<uint32_t>(data_, 0) != MH_MAGIC_64)
        return false;

    format_ = BinaryFormat::MachO;
    sections_.clear();
    segments_.clear();
    symbols_.clear();
    indirect_symbols_.clear();
    chained_fixups_.clear();
    chained_fixups_present_ = false;
    uint32_t ncmds = read_le<uint32_t>(data_, 16);
    uint32_t sizeofcmds = read_le<uint32_t>(data_, 20);
    if (ncmds > 65536 || 32ULL + sizeofcmds > data_.size()) return false;

    uint32_t symoff = 0, nsyms = 0, stroff = 0, strsize = 0;
    uint32_t indirectoff = 0, nindirect = 0;
    uint32_t chained_fixups_offset = 0, chained_fixups_size = 0;
    uint64_t entry_file_offset = 0;
    size_t command_offset = 32;
    for (uint32_t index = 0; index < ncmds; ++index) {
        if (command_offset + 8 > data_.size()) return false;
        uint32_t command = read_le<uint32_t>(data_, command_offset);
        uint32_t command_size = read_le<uint32_t>(data_, command_offset + 4);
        if (command_size < 8 || command_offset + command_size > 32ULL + sizeofcmds)
            return false;
        if (command == LC_SEGMENT_64) {
            if (command_size < 72) return false;
            BinarySegment segment;
            segment.name = fixed_name(data_, command_offset + 8, 16);
            segment.virtual_address = read_le<uint64_t>(data_, command_offset + 24);
            segment.virtual_size = read_le<uint64_t>(data_, command_offset + 32);
            segment.file_offset = read_le<uint64_t>(data_, command_offset + 40);
            segment.file_size = read_le<uint64_t>(data_, command_offset + 48);
            segment.max_protection = read_le<uint32_t>(data_, command_offset + 56);
            segment.init_protection = read_le<uint32_t>(data_, command_offset + 60);
            segment.header_offset = command_offset;
            uint32_t section_count = read_le<uint32_t>(data_, command_offset + 64);
            if (72ULL + (uint64_t)section_count * 80 > command_size) return false;
            for (uint32_t section_index = 0; section_index < section_count; ++section_index) {
                size_t section_offset = command_offset + 72 + (size_t)section_index * 80;
                BinarySection section;
                section.name = fixed_name(data_, section_offset, 16);
                section.segment_name = fixed_name(data_, section_offset + 16, 16);
                section.virtual_address = read_le<uint64_t>(data_, section_offset + 32);
                section.size = read_le<uint64_t>(data_, section_offset + 40);
                section.offset = read_le<uint32_t>(data_, section_offset + 48);
                section.alignment = read_le<uint32_t>(data_, section_offset + 52);
                uint32_t relocation_offset = read_le<uint32_t>(data_, section_offset + 56);
                uint32_t relocation_count = read_le<uint32_t>(data_, section_offset + 60);
                section.reserved1 = read_le<uint32_t>(data_, section_offset + 68);
                section.reserved2 = read_le<uint32_t>(data_, section_offset + 72);
                uint32_t flags = read_le<uint32_t>(data_, section_offset + 64);
                uint32_t section_type = flags & 0xff;
                section.zero_fill = section_type == 0x1 || section_type == 0xc ||
                                    section_type == 0x12;
                section.header_offset = section_offset;
                if (relocation_count > 1000000 ||
                    relocation_offset + (uint64_t)relocation_count * 8 > data_.size())
                    return false;
                for (uint32_t r = 0; r < relocation_count; ++r) {
                    size_t roff = relocation_offset + (size_t)r * 8;
                    uint32_t word = read_le<uint32_t>(data_, roff + 4);
                    BinaryRelocation relocation;
                    relocation.address = read_le<uint32_t>(data_, roff);
                    relocation.symbol_index = word & 0x00ffffff;
                    relocation.external = ((word >> 27) & 1) != 0;
                    relocation.type = (int)((word >> 28) & 0xf);
                    section.relocations.push_back(relocation);
                }
                sections_.push_back(std::move(section));
            }
            segments_.push_back(std::move(segment));
        } else if (command == LC_SYMTAB && command_size >= 24) {
            symoff = read_le<uint32_t>(data_, command_offset + 8);
            nsyms = read_le<uint32_t>(data_, command_offset + 12);
            stroff = read_le<uint32_t>(data_, command_offset + 16);
            strsize = read_le<uint32_t>(data_, command_offset + 20);
        } else if (command == LC_DYSYMTAB && command_size >= 80) {
            indirectoff = read_le<uint32_t>(data_, command_offset + 56);
            nindirect = read_le<uint32_t>(data_, command_offset + 60);
        } else if (command == LC_MAIN && command_size >= 24) {
            entry_file_offset = read_le<uint64_t>(data_, command_offset + 8);
        } else if (command == LC_DYLD_CHAINED_FIXUPS && command_size >= 16) {
            chained_fixups_offset = read_le<uint32_t>(data_, command_offset + 8);
            chained_fixups_size = read_le<uint32_t>(data_, command_offset + 12);
        }
        command_offset += command_size;
    }

    parse_macho_tables(symoff, nsyms, stroff, strsize, indirectoff, nindirect);
    if (chained_fixups_offset || chained_fixups_size)
        parse_macho_chained_fixups(chained_fixups_offset, chained_fixups_size);
    auto entry = offset_to_virtual_address(entry_file_offset);
    entrypoint_ = entry ? *entry : entry_file_offset;
    return true;
}

void BinaryImage::parse_macho_chained_fixups(uint32_t offset, uint32_t size) {
    chained_fixups_present_ = true;
    if ((uint64_t)offset + size > data_.size() || size < 28) return;

    const size_t base = offset;
    const uint32_t starts_offset = read_le<uint32_t>(data_, base + 4);
    const uint32_t imports_offset = read_le<uint32_t>(data_, base + 8);
    const uint32_t symbols_offset = read_le<uint32_t>(data_, base + 12);
    const uint32_t imports_count = read_le<uint32_t>(data_, base + 16);
    const uint32_t imports_format = read_le<uint32_t>(data_, base + 20);
    if (starts_offset >= size || (imports_count && imports_offset >= size) ||
        symbols_offset >= size)
        return;

    struct Import {
        std::string name;
        int64_t addend = 0;
    };
    std::vector<Import> imports;
    imports.reserve(std::min<uint32_t>(imports_count, 1000000));
    auto string_at = [&](uint64_t relative) {
        if (relative >= size || (uint64_t)symbols_offset + relative >= size)
            return std::string();
        size_t start = base + (size_t)symbols_offset + (size_t)relative;
        size_t limit = base + size;
        size_t end = start;
        while (end < limit && data_[end] != 0) ++end;
        return std::string((const char *)data_.data() + start, end - start);
    };
    if (imports_count <= 1000000) {
        size_t entry_size = imports_format == 3 ? 16 : 4;
        if (imports_format == 2) entry_size = 8;
        if ((uint64_t)imports_offset + (uint64_t)imports_count * entry_size <= size) {
            for (uint32_t i = 0; i < imports_count; ++i) {
                size_t import_offset = base + (size_t)imports_offset +
                                       (size_t)i * entry_size;
                Import item;
                uint64_t name_offset = 0;
                if (imports_format == 3) {
                    uint64_t raw = read_le<uint64_t>(data_, import_offset);
                    item.addend = read_le<int64_t>(data_, import_offset + 8);
                    name_offset = (raw >> 17) & 0x7fffffffULL;
                } else {
                    uint32_t raw = read_le<uint32_t>(data_, import_offset);
                    item.addend = imports_format == 2
                        ? read_le<int32_t>(data_, import_offset + 4)
                        : (int64_t)(int8_t)((raw >> 24) & 0xffU);
                    name_offset = (raw >> 9) & 0x7fffffU;
                }
                item.name = string_at(name_offset);
                imports.push_back(std::move(item));
            }
        }
    }

    uint64_t image_base = UINT64_MAX;
    for (const auto &segment : segments_)
        if (segment.file_size)
            image_base = std::min(image_base, segment.virtual_address);
    if (image_base == UINT64_MAX) image_base = 0;

    auto decode_next = [](uint64_t raw, uint16_t format) -> uint16_t {
        (void)format;
        return (uint16_t)((raw >> 51) & 0x7ffU);
    };
    auto decode_bind = [](uint64_t raw, uint16_t format) {
        return format == 1 || format == 9
            ? (uint32_t)(raw & 0xffffU)
            : (uint32_t)(raw & 0xffffffU);
    };
    auto decode_target = [&](uint64_t raw, uint16_t format) {
        uint64_t target = 0;
        if (format == 1) {
            target = raw & 0x7ffffffffffULL;
            target |= ((raw >> 43) & 0xffULL) << 56;
        } else if (format == 9 || format == 12) {
            target = raw & 0xffffffffULL;
            target += image_base;
        } else {
            target = raw & 0xfffffffffULL;
            target |= ((raw >> 36) & 0xffULL) << 56;
            if (format == 6) target += image_base;
        }
        return target;
    };

    if ((uint64_t)starts_offset + 4 > size) return;
    size_t starts = base + starts_offset;
    uint32_t segment_count = read_le<uint32_t>(data_, starts);
    if (segment_count > 4096 || (uint64_t)starts_offset + 4ULL +
        (uint64_t)segment_count * 4 > size) return;

    for (uint32_t segment_index = 0; segment_index < segment_count; ++segment_index) {
        uint32_t segment_info_offset = read_le<uint32_t>(
            data_, starts + 4 + (size_t)segment_index * 4);
        if (!segment_info_offset || segment_info_offset + 22 > size ||
            segment_index >= segments_.size()) continue;
        size_t info = base + segment_info_offset;
        uint32_t info_size = read_le<uint32_t>(data_, info);
        uint16_t page_size = read_le<uint16_t>(data_, info + 4);
        uint16_t pointer_format = read_le<uint16_t>(data_, info + 6);
        uint16_t page_count = read_le<uint16_t>(data_, info + 20);
        if (info_size < 22 || page_size == 0 || page_count > 65535 ||
            (uint64_t)segment_info_offset + info_size > size ||
            (uint64_t)segment_info_offset + 22ULL + (uint64_t)page_count * 2 > size)
            continue;

        const auto &segment = segments_[segment_index];
        auto walk_chain = [&](uint16_t page, uint32_t chain_offset) {
            if (chain_offset >= page_size) return;
            for (uint32_t count = 0; count < 100000; ++count) {
                uint64_t address = segment.virtual_address +
                    (uint64_t)page * page_size + chain_offset;
                auto file_offset = virtual_address_to_offset(address);
                if (!file_offset || *file_offset > data_.size() ||
                    8 > data_.size() - *file_offset) break;
                uint64_t raw = read_le<uint64_t>(data_, (size_t)*file_offset);
                BinaryChainedFixup fixup;
                fixup.address = address;
                fixup.raw = raw;
                fixup.pointer_format = pointer_format;
                fixup.next = decode_next(raw, pointer_format);
                fixup.authenticated = (pointer_format == 1 || pointer_format == 9 ||
                                       pointer_format == 12) && ((raw >> 63) & 1);
                fixup.bind = (raw >> 62) & 1;
                if (fixup.bind) {
                    fixup.import_ordinal = decode_bind(raw, pointer_format);
                    if (fixup.import_ordinal < imports.size()) {
                        fixup.symbol = imports[fixup.import_ordinal].name;
                        fixup.addend = imports[fixup.import_ordinal].addend;
                    }
                } else {
                    fixup.target = decode_target(raw, pointer_format);
                }
                chained_fixups_.push_back(std::move(fixup));
                uint16_t next = decode_next(raw, pointer_format);
                if (next == 0) break;
                chain_offset += (uint32_t)next * 4U;
                if (chain_offset >= page_size) break;
            }
        };
        const size_t overflow_base = info + 22 + (size_t)page_count * 2;
        const uint32_t overflow_count = info_size >= 22U + (uint32_t)page_count * 2U
            ? (info_size - 22U - (uint32_t)page_count * 2U) / 2U : 0;
        for (uint16_t page = 0; page < page_count; ++page) {
            uint16_t page_start = read_le<uint16_t>(
                data_, info + 22 + (size_t)page * 2);
            if (page_start == 0xffffU) continue;
            if (page_start & 0x8000U) {
                uint32_t overflow_index = page_start & 0x7fffU;
                while (overflow_index < overflow_count) {
                    uint16_t overflow_start = read_le<uint16_t>(
                        data_, overflow_base + (size_t)overflow_index * 2);
                    if (overflow_start == 0xffffU) break;
                    walk_chain(page, overflow_start & 0x7fffU);
                    if (overflow_start & 0x8000U) break;
                    ++overflow_index;
                }
            } else {
                walk_chain(page, page_start);
            }
        }
    }
    std::sort(chained_fixups_.begin(), chained_fixups_.end(),
              [](const BinaryChainedFixup &a, const BinaryChainedFixup &b) {
                  return a.address < b.address;
              });
}

void BinaryImage::update_macho_chained_fixups(uint32_t segment_index) {
    constexpr uint32_t LC_SEGMENT_64 = 0x19;
    constexpr uint32_t LC_SYMTAB = 0x2;
    constexpr uint32_t LC_DYSYMTAB = 0xb;
    constexpr uint32_t LC_DYLD_INFO = 0x22;
    constexpr uint32_t LC_DYLD_INFO_ONLY = 0x80000022;
    constexpr uint32_t LC_DYLD_CHAINED_FIXUPS = 0x80000034;
    constexpr uint32_t LC_CODE_SIGNATURE = 0x1d;
    constexpr uint32_t LC_FUNCTION_STARTS = 0x26;
    constexpr uint32_t LC_DATA_IN_CODE = 0x29;
    constexpr uint32_t LC_DYLIB_CODE_SIGN_DRS = 0x2b;
    constexpr uint32_t LC_LINKER_OPTIMIZATION_HINT = 0x2e;
    constexpr uint32_t LC_DYLD_EXPORTS_TRIE = 0x80000033;
    constexpr uint32_t LC_DYLD_ENVIRONMENT = 0x21;
    constexpr uint32_t LC_DYLD_CODE_SIGN_DRS = 0x2c;

    uint32_t ncmds = read_le<uint32_t>(data_, 16);
    uint32_t sizeofcmds = read_le<uint32_t>(data_, 20);
    size_t command_offset = 32;
    uint32_t fixup_data_offset = 0;
    uint32_t fixup_size = 0;
    size_t fixup_command = 0;
    for (uint32_t index = 0; index < ncmds; ++index) {
        uint32_t command = read_le<uint32_t>(data_, command_offset);
        uint32_t command_size = read_le<uint32_t>(data_, command_offset + 4);
        if (command == LC_DYLD_CHAINED_FIXUPS && command_size >= 16) {
            fixup_data_offset = read_le<uint32_t>(data_, command_offset + 8);
            fixup_size = read_le<uint32_t>(data_, command_offset + 12);
            fixup_command = command_offset;
            break;
        }
        command_offset += command_size;
    }
    if (!fixup_command || (uint64_t)fixup_data_offset + fixup_size > data_.size() ||
        fixup_size < 28)
        return;

    size_t blob = fixup_data_offset;
    uint32_t starts_offset = read_le<uint32_t>(data_, blob + 4);
    uint32_t imports_offset = read_le<uint32_t>(data_, blob + 8);
    uint32_t symbols_offset = read_le<uint32_t>(data_, blob + 12);
    if (starts_offset + 4 > fixup_size ||
        (uint64_t)starts_offset + 4ULL > fixup_size)
        return;
    size_t starts = blob + starts_offset;
    uint32_t old_count = read_le<uint32_t>(data_, starts);
    if (old_count > 4096 || (uint64_t)starts_offset + 4ULL +
        (uint64_t)old_count * 4 > fixup_size)
        return;
    uint32_t insert_index = std::min(segment_index, old_count);
    uint32_t relative_insert = starts_offset + 4 + insert_index * 4;
    if (relative_insert > fixup_size) return;
    std::vector<uint32_t> old_starts;
    old_starts.reserve(old_count);
    for (uint32_t index = 0; index < old_count; ++index)
        old_starts.push_back(read_le<uint32_t>(data_, starts + 4 + index * 4));

    size_t insertion_point = blob + relative_insert;
    data_.insert(data_.begin() + insertion_point, 4, 0);

    auto adjust32 = [&](size_t offset) {
        uint32_t value = read_le<uint32_t>(data_, offset);
        if (value >= insertion_point && value != 0)
            write_le<uint32_t>(data_, offset, value + 4);
    };
    auto adjust64 = [&](size_t offset) {
        uint64_t value = read_le<uint64_t>(data_, offset);
        if (value >= insertion_point && value != 0)
            write_le<uint64_t>(data_, offset, value + 4);
    };

    command_offset = 32;
    for (uint32_t index = 0; index < ncmds; ++index) {
        uint32_t command = read_le<uint32_t>(data_, command_offset);
        uint32_t command_size = read_le<uint32_t>(data_, command_offset + 4);
        if (command == LC_SEGMENT_64 && command_size >= 72) {
            uint64_t segment_offset = read_le<uint64_t>(data_, command_offset + 40);
            uint64_t segment_size = read_le<uint64_t>(data_, command_offset + 48);
            if (segment_offset <= insertion_point &&
                insertion_point <= segment_offset + segment_size)
                write_le<uint64_t>(data_, command_offset + 48, segment_size + 4);
            else
                adjust64(command_offset + 40);
            uint32_t section_count = read_le<uint32_t>(data_, command_offset + 64);
            for (uint32_t section = 0; section < section_count; ++section) {
                size_t section_offset = command_offset + 72 + (size_t)section * 80;
                adjust32(section_offset + 48);
                adjust32(section_offset + 56);
            }
        } else if (command == LC_SYMTAB && command_size >= 24) {
            adjust32(command_offset + 8);
            adjust32(command_offset + 16);
        } else if (command == LC_DYSYMTAB && command_size >= 80) {
            for (size_t field : {32U, 40U, 48U, 56U, 64U, 72U})
                adjust32(command_offset + field);
        } else if ((command == LC_DYLD_INFO || command == LC_DYLD_INFO_ONLY) &&
                   command_size >= 48) {
            for (size_t field : {8U, 16U, 24U, 32U, 40U})
                adjust32(command_offset + field);
        } else if ((command == LC_CODE_SIGNATURE || command == LC_FUNCTION_STARTS ||
                    command == LC_DATA_IN_CODE || command == LC_DYLIB_CODE_SIGN_DRS ||
                    command == LC_LINKER_OPTIMIZATION_HINT ||
                    command == LC_DYLD_EXPORTS_TRIE ||
                    command == LC_DYLD_CHAINED_FIXUPS) && command_size >= 16) {
            adjust32(command_offset + 8);
            if (command == LC_DYLD_CHAINED_FIXUPS && command_offset == fixup_command)
                write_le<uint32_t>(data_, command_offset + 12, fixup_size + 4);
        } else if ((command == LC_DYLD_ENVIRONMENT || command == LC_DYLD_CODE_SIGN_DRS) &&
                   command_size >= 20) {
            adjust32(command_offset + 8);
        }
        command_offset += command_size;
    }

    write_le<uint32_t>(data_, blob + 8,
                       imports_offset >= relative_insert ? imports_offset + 4 : imports_offset);
    write_le<uint32_t>(data_, blob + 12,
                       symbols_offset >= relative_insert ? symbols_offset + 4 : symbols_offset);
    write_le<uint32_t>(data_, starts, old_count + 1);
    size_t new_starts = blob + starts_offset + 4;
    for (uint32_t index = 0; index <= old_count; ++index) {
        uint32_t value = 0;
        if (index != insert_index) {
            uint32_t old_index = index < insert_index ? index : index - 1;
            value = old_starts[old_index];
            if (value >= relative_insert && value != 0) value += 4;
        }
        write_le<uint32_t>(data_, new_starts + (size_t)index * 4, value);
    }
}

void BinaryImage::parse_macho_tables(uint32_t symoff, uint32_t nsyms,
                                     uint32_t stroff, uint32_t strsize,
                                     uint32_t indirectoff, uint32_t nindirect) {
    if ((uint64_t)symoff + (uint64_t)nsyms * 16 > data_.size() ||
        (uint64_t)stroff + strsize > data_.size())
        return;
    symbols_.reserve(nsyms);
    for (uint32_t i = 0; i < nsyms; ++i) {
        size_t offset = symoff + (size_t)i * 16;
        BinarySymbol symbol;
        uint32_t string_offset = read_le<uint32_t>(data_, offset);
        symbol.name = table_string(data_, stroff, strsize, string_offset);
        symbol.type = data_[offset + 4];
        symbol.section_index = data_[offset + 5];
        symbol.value = read_le<uint64_t>(data_, offset + 8);
        symbols_.push_back(std::move(symbol));
    }
    if ((uint64_t)indirectoff + (uint64_t)nindirect * 4 > data_.size()) return;
    indirect_symbols_.reserve(nindirect);
    for (uint32_t i = 0; i < nindirect; ++i) {
        uint32_t index = read_le<uint32_t>(data_, indirectoff + (size_t)i * 4);
        if ((index & 0xc0000000U) == 0 && index < symbols_.size())
            indirect_symbols_.push_back(symbols_[index].name);
        else
            indirect_symbols_.emplace_back();
    }
}

bool BinaryImage::parse_elf() {
    if (data_.size() < 64 || data_[0] != 0x7f || data_[1] != 'E' ||
        data_[2] != 'L' || data_[3] != 'F' || data_[4] != 2 || data_[5] != 1)
        return false;
    format_ = BinaryFormat::ELF;
    entrypoint_ = read_le<uint64_t>(data_, 24);
    uint64_t phoff = read_le<uint64_t>(data_, 32);
    uint64_t shoff = read_le<uint64_t>(data_, 40);
    uint16_t phentsize = read_le<uint16_t>(data_, 54);
    uint16_t phnum = read_le<uint16_t>(data_, 56);
    uint16_t shentsize = read_le<uint16_t>(data_, 58);
    uint16_t shnum = read_le<uint16_t>(data_, 60);
    uint16_t shstrndx = read_le<uint16_t>(data_, 62);
    if (phentsize < 56 || phoff + (uint64_t)phentsize * phnum > data_.size()) return false;

    segments_.clear();
    sections_.clear();
    symbols_.clear();
    indirect_symbols_.clear();
    imports_.clear();
    chained_fixups_.clear();
    chained_fixups_present_ = false;
    uint64_t dynamic_offset = 0, dynamic_size = 0;
    for (uint16_t i = 0; i < phnum; ++i) {
        size_t offset = phoff + (size_t)i * phentsize;
        uint32_t type = read_le<uint32_t>(data_, offset);
        if (type == 2) {
            dynamic_offset = read_le<uint64_t>(data_, offset + 8);
            dynamic_size = read_le<uint64_t>(data_, offset + 32);
        }
        if (type != 1) continue;
        BinarySegment segment;
        segment.file_offset = read_le<uint64_t>(data_, offset + 8);
        segment.virtual_address = read_le<uint64_t>(data_, offset + 16);
        segment.file_size = read_le<uint64_t>(data_, offset + 32);
        segment.virtual_size = read_le<uint64_t>(data_, offset + 40);
        segment.alignment = read_le<uint64_t>(data_, offset + 48);
        segment.init_protection = read_le<uint32_t>(data_, offset + 4);
        segment.max_protection = segment.init_protection;
        segment.header_offset = offset;
        segments_.push_back(std::move(segment));
    }

    parse_elf_dynamic(dynamic_offset, dynamic_size);

    if (shnum == 0 || shentsize < 64 || shoff + (uint64_t)shentsize * shnum > data_.size())
        return true;
    uint64_t names_offset = 0, names_size = 0;
    if (shstrndx < shnum) {
        size_t names_header = shoff + (size_t)shstrndx * shentsize;
        names_offset = read_le<uint64_t>(data_, names_header + 24);
        names_size = read_le<uint64_t>(data_, names_header + 32);
    }
    for (uint16_t i = 0; i < shnum; ++i) {
        size_t offset = shoff + (size_t)i * shentsize;
        BinarySection section;
        section.name = table_string(data_, names_offset, names_size,
                                    read_le<uint32_t>(data_, offset));
        section.virtual_address = read_le<uint64_t>(data_, offset + 16);
        section.offset = read_le<uint64_t>(data_, offset + 24);
        section.size = read_le<uint64_t>(data_, offset + 32);
        section.zero_fill = read_le<uint32_t>(data_, offset + 4) == 8;
        section.alignment = (uint32_t)read_le<uint64_t>(data_, offset + 48);
        section.header_offset = offset;
        sections_.push_back(std::move(section));
    }
    return true;
}

void BinaryImage::parse_elf_dynamic(uint64_t dynamic_offset, uint64_t dynamic_size) {
    if (!dynamic_offset || dynamic_offset > data_.size() ||
        dynamic_size > data_.size() - dynamic_offset)
        return;

    uint64_t symtab_va = 0, strtab_va = 0, strsz = 0, syment = 24;
    uint64_t hash_va = 0, gnu_hash_va = 0, rela_va = 0, relasz = 0, relaent = 24;
    uint64_t jmprel_va = 0, pltrelsz = 0, pltgot_va = 0;
    for (uint64_t off = dynamic_offset; off + 16 <= dynamic_offset + dynamic_size; off += 16) {
        int64_t tag = read_le<int64_t>(data_, (size_t)off);
        uint64_t value = read_le<uint64_t>(data_, (size_t)off + 8);
        if (tag == 0) break;
        switch (tag) {
        case 4: hash_va = value; break;
        case 0x6ffffef5: gnu_hash_va = value; break;
        case 5: strtab_va = value; break;
        case 6: symtab_va = value; break;
        case 7: rela_va = value; break;
        case 8: relasz = value; break;
        case 9: relaent = value; break;
        case 10: strsz = value; break;
        case 11: syment = value; break;
        case 2: pltrelsz = value; break;
        case 3: pltgot_va = value; break;
        case 23: jmprel_va = value; break;
        default: break;
        }
    }

    auto symtab_off = virtual_address_to_offset(symtab_va);
    auto strtab_off = virtual_address_to_offset(strtab_va);
    if (!symtab_off || !strtab_off || syment < 24 || !strsz ||
        *strtab_off > data_.size() || strsz > data_.size() - *strtab_off)
        return;

    uint64_t symbol_count = 0;
    if (hash_va) {
        auto hash_off = virtual_address_to_offset(hash_va);
        if (hash_off && *hash_off + 8 <= data_.size())
            symbol_count = read_le<uint32_t>(data_, (size_t)*hash_off + 4);
    }
    if (!symbol_count && gnu_hash_va) {
        auto hash_off = virtual_address_to_offset(gnu_hash_va);
        if (hash_off && *hash_off + 16 <= data_.size()) {
            uint32_t buckets = read_le<uint32_t>(data_, (size_t)*hash_off);
            uint32_t first_symbol = read_le<uint32_t>(data_, (size_t)*hash_off + 4);
            uint32_t bloom_words = read_le<uint32_t>(data_, (size_t)*hash_off + 8);
            uint64_t buckets_off = *hash_off + 16 + (uint64_t)bloom_words * 8;
            if (buckets <= 1000000 && buckets_off <= data_.size() &&
                (uint64_t)buckets * 4 <= data_.size() - buckets_off) {
                uint32_t last_symbol = 0;
                for (uint32_t i = 0; i < buckets; ++i)
                    last_symbol = std::max(last_symbol,
                        read_le<uint32_t>(data_, (size_t)buckets_off + (size_t)i * 4));
                if (last_symbol >= first_symbol) {
                    uint64_t chain_off = buckets_off + (uint64_t)buckets * 4 +
                                         (uint64_t)(last_symbol - first_symbol) * 4;
                    while (chain_off + 4 <= data_.size() && last_symbol < 1000000) {
                        uint32_t chain = read_le<uint32_t>(data_, (size_t)chain_off);
                        ++last_symbol;
                        chain_off += 4;
                        if (chain & 1) break;
                    }
                    symbol_count = last_symbol;
                }
            }
        }
    }
    if (!symbol_count && strtab_va > symtab_va)
        symbol_count = (strtab_va - symtab_va) / syment;
    symbol_count = std::min<uint64_t>(symbol_count, 1000000);
    if (*symtab_off > data_.size() || symbol_count > (data_.size() - *symtab_off) / syment)
        return;

    symbols_.reserve((size_t)symbol_count);
    for (uint64_t i = 0; i < symbol_count; ++i) {
        size_t off = (size_t)(*symtab_off + i * syment);
        BinarySymbol symbol;
        symbol.name = table_string(data_, *strtab_off, strsz,
                                   read_le<uint32_t>(data_, off));
        uint16_t section_index = read_le<uint16_t>(data_, off + 6);
        symbol.type = section_index ? 0x0e : 0;
        symbol.section_index = section_index ? 1 : 0;
        symbol.value = read_le<uint64_t>(data_, off + 8);
        symbols_.push_back(std::move(symbol));
    }

    auto parse_relas = [&](uint64_t table_va, uint64_t table_size) {
        if (!table_va || !table_size || relaent < 24) return;
        auto table_off = virtual_address_to_offset(table_va);
        if (!table_off || *table_off > data_.size() ||
            table_size > data_.size() - *table_off) return;
        uint64_t count = table_size / relaent;
        for (uint64_t i = 0; i < count; ++i) {
            size_t off = (size_t)(*table_off + i * relaent);
            uint64_t slot = read_le<uint64_t>(data_, off);
            uint64_t info = read_le<uint64_t>(data_, off + 8);
            uint32_t type = (uint32_t)info;
            uint32_t symbol_index = (uint32_t)(info >> 32);
            if (symbol_index >= symbols_.size() || symbols_[symbol_index].name.empty()) continue;
            if (type != 1025 && type != 1026 && type != 1027) continue;
            BinaryImport item;
            item.name = symbols_[symbol_index].name;
            item.slot_address = slot;
            imports_.push_back(std::move(item));
        }
    };
    parse_relas(rela_va, relasz);
    parse_relas(jmprel_va, pltrelsz);

    if (pltgot_va) {
        for (const auto &segment : segments_) {
            if ((segment.init_protection & 1) == 0 || segment.file_offset > data_.size()) continue;
            uint64_t length = std::min<uint64_t>(segment.file_size,
                                                 data_.size() - segment.file_offset);
            for (uint64_t rel = 0; rel + 16 <= length; rel += 4) {
                size_t off = (size_t)(segment.file_offset + rel);
                uint32_t adrp = read_le<uint32_t>(data_, off);
                uint32_t ldr = read_le<uint32_t>(data_, off + 4);
                uint32_t add = read_le<uint32_t>(data_, off + 8);
                uint32_t br = read_le<uint32_t>(data_, off + 12);
                if ((adrp & 0x9f00001f) != 0x90000010 ||
                    (ldr & 0xffc003ff) != 0xf9400211 ||
                    (add & 0xffc003ff) != 0x91000210 || br != 0xd61f0220)
                    continue;
                int64_t imm21 = (int64_t)(((adrp >> 29) & 3) | ((adrp >> 3) & 0x1ffffc));
                if (imm21 & 0x100000) imm21 -= 0x200000;
                uint64_t pc = segment.virtual_address + rel;
                uint64_t page = (pc & ~0xfffULL) + (imm21 << 12);
                uint64_t slot = page + (((ldr >> 10) & 0xfff) * 8ULL);
                for (auto &item : imports_)
                    if (item.slot_address == slot) item.stub_address = pc;
            }
        }
    }
}

BinarySection *BinaryImage::section(const std::string &name) {
    for (auto &item : sections_)
        if (item.name == name) return &item;
    return nullptr;
}

const BinarySection *BinaryImage::section(const std::string &name) const {
    for (auto &item : sections_)
        if (item.name == name) return &item;
    return nullptr;
}

std::optional<uint64_t> BinaryImage::virtual_address_to_offset(uint64_t va) const {
    for (const auto &segment : segments_) {
        if (segment.virtual_address <= va && va - segment.virtual_address < segment.file_size)
            return segment.file_offset + (va - segment.virtual_address);
    }
    for (const auto &section : sections_) {
        if (section.virtual_address <= va && va - section.virtual_address < section.size)
            return section.offset + (va - section.virtual_address);
    }
    return std::nullopt;
}

std::optional<uint64_t> BinaryImage::offset_to_virtual_address(uint64_t offset) const {
    for (const auto &segment : segments_) {
        if (segment.file_offset <= offset && offset - segment.file_offset < segment.file_size)
            return segment.virtual_address + (offset - segment.file_offset);
    }
    for (const auto &section : sections_) {
        if (section.offset <= offset && offset - section.offset < section.size)
            return section.virtual_address + (offset - section.offset);
    }
    return std::nullopt;
}

std::vector<uint8_t> BinaryImage::content_from_virtual_address(uint64_t va, size_t size) const {
    auto offset = virtual_address_to_offset(va);
    if (!offset || *offset > data_.size() || size > data_.size() - *offset) return {};
    return std::vector<uint8_t>(data_.begin() + *offset, data_.begin() + *offset + size);
}

const BinarySymbol *BinaryImage::symbol(size_t index) const {
    return index < symbols_.size() ? &symbols_[index] : nullptr;
}

const std::string *BinaryImage::indirect_symbol(size_t index) const {
    return index < indirect_symbols_.size() ? &indirect_symbols_[index] : nullptr;
}

const BinaryChainedFixup *BinaryImage::chained_fixup(uint64_t address) const {
    auto it = std::lower_bound(
        chained_fixups_.begin(), chained_fixups_.end(), address,
        [](const BinaryChainedFixup &item, uint64_t value) {
            return item.address < value;
        });
    return it != chained_fixups_.end() && it->address == address ? &*it : nullptr;
}

std::optional<uint64_t> BinaryImage::symbol_address(const std::string &name) const {
    for (const auto &symbol : symbols_)
        if (symbol.name == name && !symbol.undefined() && symbol.value)
            return symbol.value;
    return std::nullopt;
}

std::optional<uint64_t> BinaryImage::import_slot(const std::string &name) const {
    for (const auto &item : imports_)
        if (item.name == name && item.slot_address) return item.slot_address;
    return std::nullopt;
}

std::optional<uint64_t> BinaryImage::import_stub(const std::string &name) const {
    for (const auto &item : imports_)
        if (item.name == name && item.stub_address) return item.stub_address;
    return std::nullopt;
}

void BinaryImage::add_executable_section(const std::string &name, int size,
                                         const std::vector<uint8_t> &content,
                                         bool writable) {
    if (size < 0 || content.size() > (size_t)size)
        throw std::runtime_error("invalid executable section size");
    if (auto *existing = section(name)) {
        update_existing_section(*existing, size, content);
        return;
    }
    if (is_macho()) add_macho_section(name, size, content, writable);
    else add_elf_section(name, size, content, writable);
}

void BinaryImage::update_existing_section(BinarySection &item, int size,
                                          const std::vector<uint8_t> &content) {
    if ((uint64_t)size > item.size)
        throw std::runtime_error("cannot grow an existing embedded section");
    if (item.offset > data_.size() || (uint64_t)size > data_.size() - item.offset)
        throw std::runtime_error("embedded section is outside the file");
    std::fill(data_.begin() + item.offset, data_.begin() + item.offset + size, 0);
    std::copy(content.begin(), content.end(), data_.begin() + item.offset);
}

void BinaryImage::add_macho_section(const std::string &name, int size,
                                    const std::vector<uint8_t> &content,
                                    bool writable) {
    (void)writable;
    constexpr uint32_t LC_SEGMENT_64 = 0x19;
    constexpr uint32_t LC_CODE_SIGNATURE = 0x1d;
    constexpr size_t command_size = 72 + 80;
    const uint64_t page_size = read_le<uint32_t>(data_, 4) == 0x0100000c
        ? 0x4000 : 0x1000;
    uint32_t ncmds = read_le<uint32_t>(data_, 16);
    uint32_t sizeofcmds = read_le<uint32_t>(data_, 20);

    size_t load_offset = 32;
    for (uint32_t index = 0; index < ncmds; ++index) {
        uint32_t command = read_le<uint32_t>(data_, load_offset);
        uint32_t load_size = read_le<uint32_t>(data_, load_offset + 4);
        if (load_size < 8 || load_offset + load_size > 32ULL + sizeofcmds)
            throw std::runtime_error("invalid Mach-O load command");
        if (command == LC_CODE_SIGNATURE && load_size >= 16) {
            uint32_t signature_offset = read_le<uint32_t>(data_, load_offset + 8);
            uint32_t signature_size = read_le<uint32_t>(data_, load_offset + 12);
            if ((uint64_t)signature_offset + signature_size == data_.size()) {
                for (auto &segment : segments_) {
                    uint64_t segment_end = segment.file_offset + segment.file_size;
                    if (segment.file_offset <= signature_offset && signature_offset <= segment_end) {
                        uint64_t new_size = signature_offset - segment.file_offset;
                        write_le<uint64_t>(data_, segment.header_offset + 48, new_size);
                        write_le<uint64_t>(data_, segment.header_offset + 32,
                                           align_up(new_size, page_size));
                        break;
                    }
                }
                data_.resize(signature_offset);
            }
            size_t commands_end = 32 + sizeofcmds;
            memmove(data_.data() + load_offset, data_.data() + load_offset + load_size,
                    commands_end - (load_offset + load_size));
            std::fill(data_.begin() + commands_end - load_size,
                      data_.begin() + commands_end, 0);
            --ncmds;
            sizeofcmds -= load_size;
            write_le<uint32_t>(data_, 16, ncmds);
            write_le<uint32_t>(data_, 20, sizeofcmds);
            if (!parse_macho()) throw std::runtime_error("failed to remove Mach-O signature");
            break;
        }
        load_offset += load_size;
    }

    size_t commands_end = 32 + sizeofcmds;
    size_t command_offset = commands_end;
    uint32_t inserted_segment_index = 0;
    load_offset = 32;
    for (uint32_t index = 0; index < ncmds; ++index) {
        uint32_t command = read_le<uint32_t>(data_, load_offset);
        uint32_t load_size = read_le<uint32_t>(data_, load_offset + 4);
        if (command == LC_SEGMENT_64 &&
            fixed_name(data_, load_offset + 8, 16) == "__LINKEDIT") {
            command_offset = load_offset;
            break;
        }
        if (command == LC_SEGMENT_64) ++inserted_segment_index;
        load_offset += load_size;
    }
    uint64_t first_content = data_.size();
    for (const auto &item : sections_)
        if (item.offset != 0) first_content = std::min(first_content, item.offset);
    if (commands_end + command_size > first_content)
        throw std::runtime_error("Mach-O header has no room for another segment command");

    uint64_t next_va = 0;
    uint64_t file_end = data_.size();
    BinarySegment *linkedit = nullptr;
    for (const auto &segment : segments_) {
        next_va = std::max(next_va, segment.virtual_address + segment.virtual_size);
        file_end = std::max(file_end, segment.file_offset + segment.file_size);
    }
    uint64_t virtual_size = align_up((uint64_t)std::max(size, 1), page_size);
    for (auto &segment : segments_)
        if (segment.name == "__LINKEDIT") linkedit = &segment;

    uint64_t file_offset;
    uint64_t segment_file_size;
    if (linkedit) {
        file_offset = linkedit->file_offset;
        next_va = linkedit->virtual_address;
        segment_file_size = (uint64_t)size;
        uint64_t insertion_size = virtual_size;

        auto adjust32 = [&](size_t offset) {
            uint32_t value = read_le<uint32_t>(data_, offset);
            if (value >= file_offset && value != 0)
                write_le<uint32_t>(data_, offset, value + (uint32_t)insertion_size);
        };
        load_offset = 32;
        for (uint32_t index = 0; index < ncmds; ++index) {
            uint32_t command = read_le<uint32_t>(data_, load_offset);
            uint32_t load_size = read_le<uint32_t>(data_, load_offset + 4);
            if (command == LC_SEGMENT_64 && load_size >= 72) {
                uint64_t segment_offset = read_le<uint64_t>(data_, load_offset + 40);
                if (segment_offset >= file_offset && segment_offset != 0)
                    write_le<uint64_t>(data_, load_offset + 40,
                                       segment_offset + insertion_size);
                if (fixed_name(data_, load_offset + 8, 16) == "__LINKEDIT")
                    write_le<uint64_t>(data_, load_offset + 24,
                                       read_le<uint64_t>(data_, load_offset + 24) + virtual_size);
                uint32_t section_count = read_le<uint32_t>(data_, load_offset + 64);
                for (uint32_t section_index = 0; section_index < section_count; ++section_index) {
                    size_t section_offset = load_offset + 72 + (size_t)section_index * 80;
                    adjust32(section_offset + 48);
                    adjust32(section_offset + 56);
                }
            } else if (command == 0x2 && load_size >= 24) {
                adjust32(load_offset + 8);
                adjust32(load_offset + 16);
            } else if (command == 0xb && load_size >= 80) {
                for (size_t field : {32U, 40U, 48U, 56U, 64U, 72U})
                    adjust32(load_offset + field);
            } else if ((command == 0x22 || command == 0x80000022) && load_size >= 48) {
                for (size_t field : {8U, 16U, 24U, 32U, 40U})
                    adjust32(load_offset + field);
            } else if ((command == 0x1d || command == 0x26 || command == 0x29 ||
                        command == 0x2b || command == 0x2e ||
                        command == 0x80000033 || command == 0x80000034) &&
                       load_size >= 16) {
                adjust32(load_offset + 8);
            } else if ((command == 0x21 || command == 0x2c) && load_size >= 20) {
                adjust32(load_offset + 8);
            }
            load_offset += load_size;
        }
        data_.insert(data_.begin() + file_offset, (size_t)insertion_size, 0);
    } else {
        next_va = align_up(next_va, page_size);
        file_offset = align_up(file_end, page_size);
        segment_file_size = (uint64_t)size;
        if (data_.size() < file_offset + segment_file_size)
            data_.resize((size_t)(file_offset + segment_file_size), 0);
    }
    if (file_offset > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Mach-O section offset is too large");

    memmove(data_.data() + command_offset + command_size,
            data_.data() + command_offset, commands_end - command_offset);
    std::fill(data_.begin() + command_offset,
              data_.begin() + command_offset + command_size, 0);
    std::fill(data_.begin() + file_offset, data_.begin() + file_offset + size, 0);
    std::copy(content.begin(), content.end(), data_.begin() + file_offset);

    write_le<uint32_t>(data_, command_offset, LC_SEGMENT_64);
    write_le<uint32_t>(data_, command_offset + 4, (uint32_t)command_size);
    put_fixed_name(data_, command_offset + 8, 16, name);
    write_le<uint64_t>(data_, command_offset + 24, next_va);
    write_le<uint64_t>(data_, command_offset + 32, (uint64_t)size);
    write_le<uint64_t>(data_, command_offset + 40, file_offset);
    write_le<uint64_t>(data_, command_offset + 48, segment_file_size);
    write_le<uint32_t>(data_, command_offset + 56, 5);
    write_le<uint32_t>(data_, command_offset + 60, 5);
    write_le<uint32_t>(data_, command_offset + 64, 1);
    write_le<uint32_t>(data_, command_offset + 68, 0);

    size_t section_offset = command_offset + 72;
    put_fixed_name(data_, section_offset, 16, name);
    put_fixed_name(data_, section_offset + 16, 16, name);
    write_le<uint64_t>(data_, section_offset + 32, next_va);
    write_le<uint64_t>(data_, section_offset + 40, (uint64_t)size);
    write_le<uint32_t>(data_, section_offset + 48, (uint32_t)file_offset);
    write_le<uint32_t>(data_, section_offset + 52, 2);
    write_le<uint32_t>(data_, section_offset + 64, 0x80000400);
    write_le<uint32_t>(data_, 16, ncmds + 1);
    write_le<uint32_t>(data_, 20, sizeofcmds + (uint32_t)command_size);
    update_macho_chained_fixups(inserted_segment_index);
    if (!parse_macho()) throw std::runtime_error("failed to reparse modified Mach-O");
}

void BinaryImage::add_elf_section(const std::string &name, int size,
                                  const std::vector<uint8_t> &content,
                                  bool writable) {
    uint64_t old_phoff = read_le<uint64_t>(data_, 32);
    uint64_t old_shoff = read_le<uint64_t>(data_, 40);
    uint16_t phentsize = read_le<uint16_t>(data_, 54);
    uint16_t phnum = read_le<uint16_t>(data_, 56);
    uint16_t shentsize = read_le<uint16_t>(data_, 58);
    uint16_t shnum = read_le<uint16_t>(data_, 60);
    uint16_t shstrndx = read_le<uint16_t>(data_, 62);
    if (phentsize < 56 || old_phoff > data_.size() ||
        (uint64_t)phnum * phentsize > data_.size() - old_phoff)
        throw std::runtime_error("ELF lacks a writable program table");
    if (phnum == std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("ELF table is full");

    const uint32_t p_flags = writable ? 6 : 5;
    const uint64_t sh_flags = writable ? 0x3 : 0x6;
    uint64_t page_size = 0x1000;
    uint64_t next_va = 0;
    for (const auto &segment : segments_) {
        next_va = std::max(next_va, segment.virtual_address + segment.virtual_size);
        if (segment.alignment > page_size && (segment.alignment & (segment.alignment - 1)) == 0)
            page_size = segment.alignment;
    }
    next_va = align_up(next_va, page_size);
    uint64_t load_offset = align_up(data_.size(), page_size);
    uint64_t ph_table_size = (uint64_t)(phnum + 1) * phentsize;
    uint64_t content_offset = load_offset + align_up(ph_table_size, 16);
    uint64_t content_va = next_va + (content_offset - load_offset);

    const bool has_sections = shnum != 0 && shentsize >= 64 && shstrndx < shnum &&
        old_shoff <= data_.size() && (uint64_t)shnum * shentsize <= data_.size() - old_shoff;
    if (!has_sections) {
        std::vector<uint8_t> names(1, 0);
        uint32_t name_offset = (uint32_t)names.size();
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(0);
        uint32_t shstr_name_offset = (uint32_t)names.size();
        const char shstr_name[] = ".shstrtab";
        names.insert(names.end(), shstr_name, shstr_name + sizeof(shstr_name));
        uint64_t names_offset = content_offset + size;
        uint64_t section_table_offset = align_up(names_offset + names.size(), 8);
        uint64_t end_offset = section_table_offset + 3 * 64;
        uint64_t load_size = names_offset - load_offset;
        std::vector<uint8_t> old_phdrs(data_.begin() + old_phoff,
                                       data_.begin() + old_phoff + (uint64_t)phnum * phentsize);
        data_.resize((size_t)end_offset, 0);
        std::copy(old_phdrs.begin(), old_phdrs.end(), data_.begin() + load_offset);
        for (uint16_t i = 0; i < phnum; ++i) {
            size_t offset = load_offset + (size_t)i * phentsize;
            if (read_le<uint32_t>(data_, offset) == 6) {
                write_le<uint64_t>(data_, offset + 8, load_offset);
                write_le<uint64_t>(data_, offset + 16, next_va);
                write_le<uint64_t>(data_, offset + 24, next_va);
                write_le<uint64_t>(data_, offset + 32, ph_table_size);
                write_le<uint64_t>(data_, offset + 40, ph_table_size);
                write_le<uint64_t>(data_, offset + 48, 8);
            }
        }
        size_t new_phdr = load_offset + (size_t)phnum * phentsize;
        write_le<uint32_t>(data_, new_phdr, 1);
        write_le<uint32_t>(data_, new_phdr + 4, p_flags);
        write_le<uint64_t>(data_, new_phdr + 8, load_offset);
        write_le<uint64_t>(data_, new_phdr + 16, next_va);
        write_le<uint64_t>(data_, new_phdr + 24, next_va);
        write_le<uint64_t>(data_, new_phdr + 32, load_size);
        write_le<uint64_t>(data_, new_phdr + 40, load_size);
        write_le<uint64_t>(data_, new_phdr + 48, page_size);
        std::copy(content.begin(), content.end(), data_.begin() + content_offset);
        std::copy(names.begin(), names.end(), data_.begin() + names_offset);
        size_t content_shdr = section_table_offset + 64;
        write_le<uint32_t>(data_, content_shdr, name_offset);
        write_le<uint32_t>(data_, content_shdr + 4, 1);
        write_le<uint64_t>(data_, content_shdr + 8, sh_flags);
        write_le<uint64_t>(data_, content_shdr + 16, content_va);
        write_le<uint64_t>(data_, content_shdr + 24, content_offset);
        write_le<uint64_t>(data_, content_shdr + 32, (uint64_t)size);
        write_le<uint64_t>(data_, content_shdr + 48, 16);
        size_t names_shdr = section_table_offset + 128;
        write_le<uint32_t>(data_, names_shdr, shstr_name_offset);
        write_le<uint32_t>(data_, names_shdr + 4, 3);
        write_le<uint64_t>(data_, names_shdr + 24, names_offset);
        write_le<uint64_t>(data_, names_shdr + 32, (uint64_t)names.size());
        write_le<uint64_t>(data_, names_shdr + 48, 1);
        write_le<uint64_t>(data_, 32, load_offset);
        write_le<uint64_t>(data_, 40, section_table_offset);
        write_le<uint16_t>(data_, 56, phnum + 1);
        write_le<uint16_t>(data_, 58, 64);
        write_le<uint16_t>(data_, 60, 3);
        write_le<uint16_t>(data_, 62, 2);
        if (!parse_elf()) throw std::runtime_error("failed to reparse modified ELF");
        return;
    }
    if (shnum == std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("ELF section table is full");

    size_t old_names_header = old_shoff + (size_t)shstrndx * shentsize;
    uint64_t old_names_offset = read_le<uint64_t>(data_, old_names_header + 24);
    uint64_t old_names_size = read_le<uint64_t>(data_, old_names_header + 32);
    if (old_names_offset > data_.size() || old_names_size > data_.size() - old_names_offset)
        throw std::runtime_error("invalid ELF section name table");
    std::vector<uint8_t> names(data_.begin() + old_names_offset,
                               data_.begin() + old_names_offset + old_names_size);
    if (names.empty()) names.push_back(0);
    uint32_t name_offset = (uint32_t)names.size();
    names.insert(names.end(), name.begin(), name.end());
    names.push_back(0);

    uint64_t names_offset = content_offset + size;
    uint64_t section_table_offset = align_up(names_offset + names.size(), 8);
    uint64_t end_offset = section_table_offset + (uint64_t)(shnum + 1) * shentsize;
    uint64_t load_size = names_offset - load_offset;

    std::vector<uint8_t> old_phdrs(data_.begin() + old_phoff,
                                   data_.begin() + old_phoff + (uint64_t)phnum * phentsize);
    std::vector<uint8_t> old_shdrs(data_.begin() + old_shoff,
                                   data_.begin() + old_shoff + (uint64_t)shnum * shentsize);
    data_.resize((size_t)end_offset, 0);
    std::copy(old_phdrs.begin(), old_phdrs.end(), data_.begin() + load_offset);

    for (uint16_t i = 0; i < phnum; ++i) {
        size_t offset = load_offset + (size_t)i * phentsize;
        if (read_le<uint32_t>(data_, offset) == 6) {
            write_le<uint64_t>(data_, offset + 8, load_offset);
            write_le<uint64_t>(data_, offset + 16, next_va);
            write_le<uint64_t>(data_, offset + 24, next_va);
            write_le<uint64_t>(data_, offset + 32, ph_table_size);
            write_le<uint64_t>(data_, offset + 40, ph_table_size);
            write_le<uint64_t>(data_, offset + 48, 8);
        }
    }
    size_t new_phdr = load_offset + (size_t)phnum * phentsize;
    write_le<uint32_t>(data_, new_phdr, 1);
    write_le<uint32_t>(data_, new_phdr + 4, p_flags);
    write_le<uint64_t>(data_, new_phdr + 8, load_offset);
    write_le<uint64_t>(data_, new_phdr + 16, next_va);
    write_le<uint64_t>(data_, new_phdr + 24, next_va);
    write_le<uint64_t>(data_, new_phdr + 32, load_size);
    write_le<uint64_t>(data_, new_phdr + 40, load_size);
    write_le<uint64_t>(data_, new_phdr + 48, page_size);

    std::fill(data_.begin() + content_offset, data_.begin() + content_offset + size, 0);
    std::copy(content.begin(), content.end(), data_.begin() + content_offset);
    std::copy(names.begin(), names.end(), data_.begin() + names_offset);
    std::copy(old_shdrs.begin(), old_shdrs.end(), data_.begin() + section_table_offset);

    size_t names_header = section_table_offset + (size_t)shstrndx * shentsize;
    write_le<uint64_t>(data_, names_header + 24, names_offset);
    write_le<uint64_t>(data_, names_header + 32, (uint64_t)names.size());
    size_t new_section = section_table_offset + (size_t)shnum * shentsize;
    write_le<uint32_t>(data_, new_section, name_offset);
    write_le<uint32_t>(data_, new_section + 4, 1);
    write_le<uint64_t>(data_, new_section + 8, sh_flags);
    write_le<uint64_t>(data_, new_section + 16, content_va);
    write_le<uint64_t>(data_, new_section + 24, content_offset);
    write_le<uint64_t>(data_, new_section + 32, (uint64_t)size);
    write_le<uint64_t>(data_, new_section + 48, 16);

    write_le<uint64_t>(data_, 32, load_offset);
    write_le<uint64_t>(data_, 40, section_table_offset);
    write_le<uint16_t>(data_, 56, phnum + 1);
    write_le<uint16_t>(data_, 60, shnum + 1);
    if (!parse_elf()) throw std::runtime_error("failed to reparse modified ELF");
}

std::vector<uint8_t> BinaryImage::serialized() const {
    if (!fat_) return data_;
    auto slices = fat_slices_;
    slices[selected_slice_].data = data_;
    size_t entry_size = fat64_ ? 32 : 20;
    std::vector<uint8_t> output(8 + slices.size() * entry_size, 0);
    write_be<uint32_t>(output, 0, fat64_ ? 0xcafebabf : 0xcafebabe);
    write_be<uint32_t>(output, 4, (uint32_t)slices.size());
    uint64_t cursor = output.size();
    for (size_t i = 0; i < slices.size(); ++i) {
        uint64_t alignment = slices[i].alignment < 63 ? 1ULL << slices[i].alignment : 1;
        cursor = align_up(cursor, alignment);
        if (!fat64_ && (cursor > UINT32_MAX || slices[i].data.size() > UINT32_MAX))
            throw std::runtime_error("fat Mach-O slice exceeds 32-bit limits");
        size_t entry = 8 + i * entry_size;
        write_be<int32_t>(output, entry, slices[i].cpu_type);
        write_be<int32_t>(output, entry + 4, slices[i].cpu_subtype);
        if (fat64_) {
            write_be<uint64_t>(output, entry + 8, cursor);
            write_be<uint64_t>(output, entry + 16, slices[i].data.size());
            write_be<uint32_t>(output, entry + 24, slices[i].alignment);
            write_be<uint32_t>(output, entry + 28, slices[i].reserved);
        } else {
            write_be<uint32_t>(output, entry + 8, (uint32_t)cursor);
            write_be<uint32_t>(output, entry + 12, (uint32_t)slices[i].data.size());
            write_be<uint32_t>(output, entry + 16, slices[i].alignment);
        }
        output.resize((size_t)cursor, 0);
        output.insert(output.end(), slices[i].data.begin(), slices[i].data.end());
        cursor = output.size();
    }
    return output;
}

void BinaryImage::write(const std::filesystem::path &path) const {
    auto output = serialized();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot write: " + path.string());
    if (!output.empty()) stream.write((const char *)output.data(), output.size());
}
