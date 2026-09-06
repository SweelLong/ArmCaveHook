#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
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
    bool from_new_asm_func = false;
    bool has_absolute_target = false;
    uint64_t absolute_target = 0;
};

struct PluginBlob {
    std::vector<uint8_t> text;
    std::vector<uint8_t> extra;
    std::vector<HookAction> declarations;
    std::vector<RelocEntry> relocs;
    std::map<std::string, int> section_offsets;
    std::map<std::string, int> symbol_offsets;
    std::map<std::string, int> data_symbol_offsets;
    std::map<std::string, int> function_offsets;
    std::vector<std::string> register_args;
    int entry_offset = 0;
    std::string default_segment;
    bool has_writable_extra = false;

    int total_bytes() const;
    int max_text_bytes() const;
    PluginBlob for_action(const HookAction &action) const;
    std::vector<uint8_t> build(uint64_t text_va, uint64_t data_va, const std::filesystem::path *target_binary) const;
};

std::vector<uint8_t> extract_cave_asm_save();
std::vector<uint8_t> extract_cave_asm_restore();
std::vector<uint8_t> extract_cave_asm_ret();

// Numeric branch/page operands in plugin assembly are treated as absolute
// VAs when they fall inside the target image's VA range.  The defaults keep
// the historical Mach-O behavior where only VAs >= 0x100000000 were
// recognized as absolute targets.
struct AsmVaRange {
    uint64_t min = 0x100000000ULL;
    uint64_t max = ~0ULL;

    static AsmVaRange of(const BinaryImage &binary);
};

std::vector<uint8_t> assemble_aarch64(const std::string &source,
                                      uint64_t address = 0,
                                      const std::map<std::string, uint64_t> &symbol_targets = {},
                                      const AsmVaRange &va_range = {});

PluginBlob compile_plugin(const std::filesystem::path &path,
                          const std::filesystem::path *target_binary);
