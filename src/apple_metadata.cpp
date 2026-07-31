#include "apple_metadata.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace armcave {

namespace {

std::optional<uint32_t> read_u32(const BinaryImage &binary, uint64_t address) {
    auto offset = binary.virtual_address_to_offset(address);
    if (!offset || *offset > binary.data().size() ||
        4 > binary.data().size() - *offset)
        return std::nullopt;
    uint32_t value = 0;
    std::memcpy(&value, binary.data().data() + *offset, sizeof(value));
    return value;
}

std::optional<uint64_t> read_u64(const BinaryImage &binary, uint64_t address) {
    auto offset = binary.virtual_address_to_offset(address);
    if (!offset || *offset > binary.data().size() ||
        8 > binary.data().size() - *offset)
        return std::nullopt;
    uint64_t value = 0;
    std::memcpy(&value, binary.data().data() + *offset, sizeof(value));
    return value;
}

std::optional<std::string> read_string(const BinaryImage &binary, uint64_t address) {
    auto offset = binary.virtual_address_to_offset(address);
    if (!offset || *offset >= binary.data().size()) return std::nullopt;
    size_t end = (size_t)*offset;
    size_t limit = binary.data().size();
    while (end < limit && end - (size_t)*offset < 4096 && binary.data()[end]) {
        unsigned char c = binary.data()[end];
        if (c < 0x20 || c > 0x7e) return std::nullopt;
        ++end;
    }
    if (end == limit || end - (size_t)*offset == 4096)
        return std::nullopt;
    return std::string((const char *)binary.data().data() + *offset,
                       end - (size_t)*offset);
}

std::optional<uint64_t> image_base(const BinaryImage &binary) {
    if (binary.segments().empty()) return std::nullopt;
    uint64_t value = UINT64_MAX;
    for (const auto &segment : binary.segments())
        if (segment.file_size)
            value = std::min(value, segment.virtual_address);
    if (value == UINT64_MAX) return std::nullopt;
    return value;
}

std::optional<uint64_t> normalize_pointer(const BinaryImage &binary,
                                          uint64_t address,
                                          uint64_t raw) {
    if (const auto *fixup = binary.chained_fixup(address)) {
        if (fixup->bind || !fixup->target) return std::nullopt;
        raw = fixup->target;
    }
    const uint64_t candidates[] = {
        raw,
        raw & 0x0000ffffffffffffULL,
        raw & ~0x7ULL,
    };
    for (uint64_t candidate : candidates)
        if (binary.virtual_address_to_offset(candidate)) return candidate;
    auto base = image_base(binary);
    if (base && raw < binary.data().size()) {
        uint64_t candidate = *base + raw;
        if (binary.virtual_address_to_offset(candidate)) return candidate;
    }
    return std::nullopt;
}

std::optional<uint64_t> read_pointer(const BinaryImage &binary, uint64_t address) {
    auto raw = read_u64(binary, address);
    if (!raw) return std::nullopt;
    return normalize_pointer(binary, address, *raw);
}

std::string section_name(const BinarySection &section) {
    return section.segment_name.empty() ? section.name :
        section.segment_name + "," + section.name;
}

const BinarySection *find_section(const BinaryImage &binary, const std::string &name) {
    for (const auto &section : binary.sections())
        if (section.name == name || section_name(section) == name) return &section;
    return nullptr;
}

void append_method(std::vector<ObjCMethodMetadata> &out,
                   const BinaryImage &binary,
                   const std::string &class_name,
                   uint64_t method_list) {
    auto flags = read_u32(binary, method_list);
    auto count = read_u32(binary, method_list + 4);
    if (!flags || !count || *count > 100000) return;
    uint32_t entry_size = *flags & ~3U;
    if (entry_size < 24) entry_size = 24;
    for (uint32_t index = 0; index < *count; ++index) {
        uint64_t entry = method_list + 8ULL + (uint64_t)index * entry_size;
        auto name_pointer = read_pointer(binary, entry);
        auto types_pointer = read_pointer(binary, entry + 8);
        auto imp_pointer = read_pointer(binary, entry + 16);
        if (!name_pointer || !imp_pointer) continue;
        auto selector = read_string(binary, *name_pointer);
        if (!selector) continue;
        ObjCMethodMetadata method;
        method.class_name = class_name;
        method.selector = *selector;
        method.address = *imp_pointer;
        method.method_list = method_list;
        if (types_pointer) {
            auto types = read_string(binary, *types_pointer);
            if (types) method.types = *types;
        }
        bool duplicate = false;
        for (const auto &item : out)
            if (item.class_name == method.class_name &&
                item.selector == method.selector &&
                item.address == method.address) {
                duplicate = true;
                break;
            }
        if (!duplicate) out.push_back(std::move(method));
    }
}

std::optional<std::string> class_name_at(const BinaryImage &binary, uint64_t class_address) {
    auto data = read_pointer(binary, class_address + 32);
    if (!data) return std::nullopt;
    uint64_t ro = *data & ~0x7ULL;
    auto name = read_pointer(binary, ro + 24);
    if (!name) return std::nullopt;
    return read_string(binary, *name);
}

void append_class_methods(std::vector<ObjCMethodMetadata> &out,
                          const BinaryImage &binary,
                          uint64_t class_address) {
    auto data = read_pointer(binary, class_address + 32);
    auto name = class_name_at(binary, class_address);
    if (!data || !name) return;
    uint64_t ro = *data & ~0x7ULL;
    auto methods = read_pointer(binary, ro + 32);
    if (methods) append_method(out, binary, *name, *methods);
}

void append_category_methods(std::vector<ObjCMethodMetadata> &out,
                             const BinaryImage &binary,
                             uint64_t category_address) {
    auto class_pointer = read_pointer(binary, category_address + 8);
    if (!class_pointer) return;
    auto name_pointer = read_pointer(binary, category_address);
    auto class_name = class_name_at(binary, *class_pointer);
    if (!class_name && name_pointer) class_name = read_string(binary, *name_pointer);
    if (!class_name) return;
    auto instance_methods = read_pointer(binary, category_address + 16);
    auto class_methods = read_pointer(binary, category_address + 24);
    if (instance_methods) append_method(out, binary, *class_name, *instance_methods);
    if (class_methods) append_method(out, binary, *class_name, *class_methods);
}

std::vector<ObjCMethodMetadata> enumerate_class_list(const BinaryImage &binary) {
    std::vector<ObjCMethodMetadata> out;
    auto *classes = find_section(binary, "__objc_classlist");
    if (classes) {
        for (uint64_t offset = 0; offset + 8 <= classes->size; offset += 8) {
            auto address = read_pointer(binary, classes->virtual_address + offset);
            if (address) append_class_methods(out, binary, *address);
        }
    }
    auto *categories = find_section(binary, "__objc_catlist");
    if (categories) {
        for (uint64_t offset = 0; offset + 8 <= categories->size; offset += 8) {
            auto address = read_pointer(binary, categories->virtual_address + offset);
            if (address) append_category_methods(out, binary, *address);
        }
    }
    return out;
}

bool starts_with(const std::string &value, const std::string &prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

std::vector<SwiftMetadataEntry> scan_swift_strings(const BinaryImage &binary) {
    std::vector<SwiftMetadataEntry> out;
    for (const auto &section : binary.sections()) {
        if (!starts_with(section.name, "__swift5_") || section.size == 0) continue;
        auto offset = binary.virtual_address_to_offset(section.virtual_address);
        if (!offset || *offset > binary.data().size() ||
            section.size > binary.data().size() - *offset) continue;
        uint64_t start = section.virtual_address;
        uint64_t end = start + section.size;
        uint64_t cursor = start;
        while (cursor < end) {
            auto value = read_string(binary, cursor);
            if (value && value->size() >= 2) {
                bool duplicate = false;
                for (const auto &item : out)
                    if (item.name == *value && item.address == cursor) {
                        duplicate = true;
                        break;
                    }
                if (!duplicate) out.push_back({*value, cursor});
                cursor += value->size() + 1;
            } else {
                ++cursor;
            }
        }
    }
    return out;
}

std::vector<SwiftMetadataEntry> scan_swift_types(const BinaryImage &binary) {
    std::vector<SwiftMetadataEntry> out;
    for (const auto &section : binary.sections()) {
        if (section.name != "__swift5_types" || section.size < 4) continue;
        for (uint64_t offset = 0; offset + 4 <= section.size; offset += 4) {
            uint64_t slot = section.virtual_address + offset;
            auto relative = read_u32(binary, slot);
            if (!relative) continue;
            uint64_t descriptor = slot + (uint64_t)(int64_t)(int32_t)*relative;
            if (!binary.virtual_address_to_offset(descriptor)) continue;
            auto name_relative = read_u32(binary, descriptor + 8);
            if (!name_relative) continue;
            uint64_t name_address = descriptor + 8 +
                (uint64_t)(int64_t)(int32_t)*name_relative;
            auto name = read_string(binary, name_address);
            if (!name || name->empty()) continue;
            bool duplicate = false;
            for (const auto &item : out)
                if (item.name == *name && item.address == descriptor) {
                    duplicate = true;
                    break;
                }
            if (!duplicate) out.push_back({*name, descriptor});
        }
    }
    return out;
}

}

std::vector<ObjCMethodMetadata> enumerate_objc_methods(const BinaryImage &binary) {
    return enumerate_class_list(binary);
}

std::optional<uint64_t> find_objc_method(const BinaryImage &binary,
                                         const std::string &class_name,
                                         const std::string &selector) {
    auto methods = enumerate_objc_methods(binary);
    for (const auto &method : methods)
        if (method.class_name == class_name && method.selector == selector)
            return method.address;
    return std::nullopt;
}

std::vector<SwiftMetadataEntry> enumerate_swift_metadata(const BinaryImage &binary) {
    auto result = scan_swift_strings(binary);
    auto types = scan_swift_types(binary);
    for (auto &item : types) {
        bool duplicate = false;
        for (const auto &existing : result)
            if (existing.name == item.name && existing.address == item.address) {
                duplicate = true;
                break;
            }
        if (!duplicate) result.push_back(std::move(item));
    }
    return result;
}

std::optional<uint64_t> find_swift_metadata(const BinaryImage &binary,
                                            const std::string &name) {
    auto methods = enumerate_swift_metadata(binary);
    for (const auto &item : methods)
        if (item.name == name) return item.address;
    for (const auto &symbol : binary.symbols())
        if (!symbol.undefined() && symbol.value &&
            (symbol.name == name || symbol.name.find(name) != std::string::npos))
            return symbol.value;
    return std::nullopt;
}

}
