#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include "binary_image.h"
#include "plugin.h"

struct MachO {
    std::unique_ptr<BinaryImage> bin;

    BinarySection *section(const std::string &name) {
        return bin ? bin->section(name) : nullptr;
    }
};

MachO open_macho(const std::string &path);

struct RelocEntry {
    int type = 0;
    int address = 0;
    std::string symbol_name;
    uint64_t symbol_value = 0;
    std::string symbol_section;
    int64_t addend = 0;
};

struct PluginBlob {
    std::vector<uint8_t> text;
    std::vector<uint8_t> extra;
    std::vector<HookAction> declarations;
    std::vector<RelocEntry> relocs;
    std::map<std::string, int> section_offsets;
    std::map<std::string, int> symbol_offsets;
    std::vector<std::string> register_args;
    int entry_offset = 0;
    std::string default_segment;

    int total_bytes() const;
    PluginBlob for_action(const HookAction &action) const;
    std::vector<uint8_t> build(uint64_t text_va, uint64_t data_va, const std::filesystem::path *target_binary) const;
};

std::vector<uint8_t> extract_cave_asm_save();
std::vector<uint8_t> extract_cave_asm_restore();
std::vector<uint8_t> extract_cave_asm_ret();

std::vector<uint8_t> assemble_aarch64(const std::string &source);

PluginBlob compile_plugin(const std::filesystem::path &path,
                          const std::filesystem::path *target_binary);
