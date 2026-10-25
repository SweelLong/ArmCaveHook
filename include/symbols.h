#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <map>

struct RelocEntry;
class BinaryImage;

std::vector<std::pair<std::string, std::string>> list_available_symbols(
    const std::filesystem::path &binary_path);

uint64_t find_function_address(BinaryImage *binary, const std::string &query);

std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
resolve_plugin_relocs(
    const std::vector<uint8_t> &text,
    const std::vector<uint8_t> &extra,
    const std::vector<RelocEntry> &relocs,
    const std::map<std::string, int> &offsets,
    const std::filesystem::path &binary_path,
    uint64_t text_va,
    uint64_t data_va);

// 复用已解析的镜像。pipeline 里每个插件都会走一次 reloc 解析，
// 若各自 BinaryImage::parse 一遍 30MB 目标文件，多插件时会慢上十几秒。
std::pair<std::vector<uint8_t>, std::vector<uint8_t>>
resolve_plugin_relocs(
    const std::vector<uint8_t> &text,
    const std::vector<uint8_t> &extra,
    const std::vector<RelocEntry> &relocs,
    const std::map<std::string, int> &offsets,
    BinaryImage &binary,
    uint64_t text_va,
    uint64_t data_va);
