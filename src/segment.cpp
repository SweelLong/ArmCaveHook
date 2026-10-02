#include "segment.h"
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <algorithm>

std::vector<uint8_t> read_file(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot read: " + path.string());
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(size);
    if (size > 0)
        f.read((char *)data.data(), size);
    return data;
}

void write_file(const std::filesystem::path &path, const std::vector<uint8_t> &data)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f)
        throw std::runtime_error("cannot write: " + path.string());
    if (!data.empty())
        f.write((const char *)data.data(), data.size());
}

std::vector<uint8_t> read_range(const std::filesystem::path &path, int64_t offset, int64_t length)
{
    std::vector<uint8_t> data((size_t)length);
    if (length <= 0)
        return data;
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot read: " + path.string());
    f.seekg((std::streamoff)offset);
    f.read((char *)data.data(), (std::streamsize)length);
    if (f.gcount() != (std::streamsize)length)
        throw std::runtime_error("read out of range: " + path.string());
    return data;
}

void write_range(const std::filesystem::path &path, int64_t offset, const std::vector<uint8_t> &payload)
{
    std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!f)
        throw std::runtime_error("cannot open for patch: " + path.string());
    f.seekg(0, std::ios::end);
    std::streamoff end = f.tellg();
    if (offset < 0 || offset + (int64_t)payload.size() > (int64_t)end)
        throw std::runtime_error("patch out of range: " + path.string());
    f.seekp((std::streamoff)offset);
    if (!payload.empty())
        f.write((const char *)payload.data(), (std::streamsize)payload.size());
    f.flush();
    if (!f)
        throw std::runtime_error("failed to patch: " + path.string());
}

int align(int value, int page_size)
{
    return (value + page_size - 1) & ~(page_size - 1);
}

std::string seg_name(const std::string &name, bool is_macho)
{
    if (is_macho)
    {
        if (name.size() >= 2 && name[0] == '_' && name[1] == '_')
            return name;
        return "__" + name.substr(0, 14);
    }
    if (!name.empty() && name[0] == '.')
        return name;
    return "." + name;
}

std::string seg_name(BinaryImage &binary, const std::string &name)
{
    return seg_name(name, binary.is_macho());
}

static uint64_t last_end_va(const std::vector<BinarySegment> &segments, uint64_t default_page = 0x1000)
{
    uint64_t end = 0;
    for (auto &segment : segments)
        end = std::max(end, segment.virtual_address + segment.virtual_size);
    return end > 0 ? (end + default_page - 1) & ~(default_page - 1) : 0;
}

uint64_t seg_va(BinaryImage &binary, const std::string &name, int size)
{
    (void)size;
    if (binary.is_macho())
    {
        auto sname = seg_name(name, true);
        for (auto &segment : binary.segments())
            if (segment.name == sname)
                return segment.virtual_address;
        for (auto &section : binary.sections())
            if (section.name == sname)
                return section.virtual_address;
        return last_end_va(binary.segments());
    }
    if (binary.is_elf())
    {
        auto sname = seg_name(name, false);
        auto *section = binary.section(sname);
        if (section)
            return section->virtual_address;
        return last_end_va(binary.segments());
    }
    throw std::runtime_error("unsupported binary type");
}

void add_segment(const std::filesystem::path &binary_path, const SegmentPlan &plan, const std::filesystem::path &output_path)
{
    add_segments(binary_path, {plan}, output_path);
}

void add_segments(const std::filesystem::path &binary_path, const std::vector<SegmentPlan> &plans, const std::filesystem::path &output_path)
{
    auto binary = BinaryImage::parse(binary_path);
    if (!binary)
        throw std::runtime_error("failed to parse " + binary_path.string());
    std::vector<BinarySectionPlan> pending;
    for (const auto &plan : plans)
    {
        int size = plan.size;
        auto content = plan.content;
        content.resize(size, 0);
        auto name = seg_name(*binary, plan.name);
        if (binary->section(name))
            binary->add_executable_section(name, size, content, plan.writable);
        else
            pending.push_back({name, size, std::move(content), plan.writable});
    }
    if (!pending.empty())
    {
        if (binary->is_macho())
            binary->add_macho_sections(pending);
        else
            binary->add_elf_sections(pending);
    }
    binary->write(output_path);
}

void write_at_offset(const std::filesystem::path &path, int offset, const std::vector<uint8_t> &payload, int size)
{
    if (offset < 0)
        throw std::runtime_error("write out of range");
    if (size > (int)payload.size())
    {
        std::vector<uint8_t> padded((size_t)size, 0);
        std::copy(payload.begin(), payload.end(), padded.begin());
        write_range(path, offset, padded);
        return;
    }
    write_range(path, offset, payload);
}

int segment_file_offset(BinaryImage &binary, const std::string &name)
{
    if (binary.is_macho())
    {
        auto sname = seg_name(name, true);
        for (auto &section : binary.sections())
            if (section.name == sname || section.segment_name == sname)
                return (int)section.offset;
        for (auto &segment : binary.segments())
            if (segment.name == sname)
                return (int)segment.file_offset;
        throw std::runtime_error("segment not found: " + name);
    }
    if (binary.is_elf())
    {
        auto sname = seg_name(name, false);
        auto *section = binary.section(sname);
        if (!section)
            throw std::runtime_error("section not found: " + name);
        return (int)section->offset;
    }
    throw std::runtime_error("unsupported binary type");
}

uint64_t remap_macho_offset_va(BinaryImage &before, BinaryImage &after, int file_offset)
{
    for (auto &sec : before.sections())
    {
        int start = (int)sec.offset;
        int end = start + (int)sec.size;
        if (start <= file_offset && file_offset < end)
        {
            int rel = file_offset - start;
            for (auto &s : after.sections())
            {
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
