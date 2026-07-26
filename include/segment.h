#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include "binary_image.h"

struct SegmentPlan
{
    std::string name;
    int size = 0;
    std::vector<uint8_t> content;
};

std::vector<uint8_t> read_file(const std::filesystem::path &path);
void write_file(const std::filesystem::path &path, const std::vector<uint8_t> &data);

int align(int value, int page_size = 0x1000);
std::string seg_name(const std::string &name, bool is_macho);
std::string seg_name(BinaryImage &binary, const std::string &name);

uint64_t seg_va(BinaryImage &binary, const std::string &name, int size);

void add_segment(const std::filesystem::path &binary_path,
                 const SegmentPlan &plan,
                 const std::filesystem::path &output_path);

void write_at_offset(const std::filesystem::path &path, int offset,
                     const std::vector<uint8_t> &payload, int size);

int segment_file_offset(BinaryImage &binary, const std::string &name);

uint64_t remap_macho_offset_va(BinaryImage &before,
                               BinaryImage &after,
                               int file_offset);
