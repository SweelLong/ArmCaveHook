#include "segment.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <algorithm>

std::vector<uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot read: " + path.string());
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(size);
    if (size > 0) f.read((char *)data.data(), size);
    return data;
}

void write_file(const std::filesystem::path &path, const std::vector<uint8_t> &data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write: " + path.string());
    if (!data.empty()) f.write((const char *)data.data(), data.size());
}

int align(int value, int page_size) {
    return (value + page_size - 1) & ~(page_size - 1);
}

std::string seg_name(const std::string &name, bool is_macho) {
    if (is_macho) {
        if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
            return name;
        return "__" + name.substr(0, 14);
    }
    if (!name.empty() && name[0] == '.')
        return name;
    return "." + name;
}

std::string seg_name(BinaryImage &binary, const std::string &name) {
    return seg_name(name, binary.is_macho());
}

static uint64_t last_end_va(const std::vector<BinarySegment> &segments,
                            uint64_t default_page = 0x1000) {
    uint64_t end = 0;
    for (auto &segment : segments)
        end = std::max(end, segment.virtual_address + segment.virtual_size);
    return end > 0 ? (end + default_page - 1) & ~(default_page - 1) : 0;
}

uint64_t seg_va(BinaryImage &binary, const std::string &name, int size) {
    (void)size;
    if (binary.is_macho()) {
        auto sname = seg_name(name, true);
        for (auto &segment : binary.segments())
            if (segment.name == sname) return segment.virtual_address;
        return last_end_va(binary.segments());
    }
    if (binary.is_elf()) {
        auto sname = seg_name(name, false);
        auto *section = binary.section(sname);
        if (section) return section->virtual_address;
        return last_end_va(binary.segments());
    }
    throw std::runtime_error("unsupported binary type");
}

void add_segment(const std::filesystem::path &binary_path,
                 const SegmentPlan &plan,
                 const std::filesystem::path &output_path) {
    auto binary = BinaryImage::parse(binary_path);
    if (!binary)
        throw std::runtime_error("failed to parse " + binary_path.string());
    int size = plan.size;
    auto content = plan.content;
    content.resize(size, 0);
    binary->add_executable_section(seg_name(*binary, plan.name), size, content);
    binary->write(output_path);
}

void write_at_offset(const std::filesystem::path &path, int offset,
                     const std::vector<uint8_t> &payload, int size) {
    auto data = read_file(path);
    int limit = size >= 0 ? size : (int)payload.size();
    if (offset + limit > (int)data.size())
        throw std::runtime_error("write out of range");
    memcpy(&data[offset], payload.data(), payload.size());
    if (size > (int)payload.size())
        memset(&data[offset + payload.size()], 0, size - payload.size());
    write_file(path, data);
}

int segment_file_offset(BinaryImage &binary, const std::string &name) {
    if (binary.is_macho()) {
        auto sname = seg_name(name, true);
        for (auto &section : binary.sections())
            if (section.segment_name == sname) return (int)section.offset;
        for (auto &segment : binary.segments())
            if (segment.name == sname) return (int)segment.file_offset;
        throw std::runtime_error("segment not found: " + name);
    }
    if (binary.is_elf()) {
        auto sname = seg_name(name, false);
        auto *section = binary.section(sname);
        if (!section) throw std::runtime_error("section not found: " + name);
        return (int)section->offset;
    }
    throw std::runtime_error("unsupported binary type");
}

uint64_t remap_macho_offset_va(BinaryImage &before,
                                BinaryImage &after,
                                int file_offset) {
    for (auto &sec : before.sections()) {
        int start = (int)sec.offset;
        int end = start + (int)sec.size;
        if (start <= file_offset && file_offset < end) {
            int rel = file_offset - start;
            for (auto &s : after.sections()) {
                if (s.name == sec.name && s.segment_name == sec.segment_name)
                    return s.virtual_address + rel;
            }
            break;
        }
    }
    auto va = before.offset_to_virtual_address(file_offset);
    if (!va)
        throw std::runtime_error("cannot map original file offset");
    return *va;
}
