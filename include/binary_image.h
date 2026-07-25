#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class BinaryFormat {
    MachO,
    ELF,
};

struct BinaryRelocation {
    int type = 0;
    uint64_t address = 0;
    uint32_t symbol_index = 0;
    bool external = false;
};

struct BinarySymbol {
    std::string name;
    uint64_t value = 0;
    uint8_t type = 0;
    uint8_t section_index = 0;

    bool undefined() const;
};

struct BinaryImport {
    std::string name;
    uint64_t slot_address = 0;
    uint64_t stub_address = 0;
};

struct BinarySection {
    std::string name;
    std::string segment_name;
    uint64_t virtual_address = 0;
    uint64_t size = 0;
    uint64_t offset = 0;
    uint32_t alignment = 0;
    uint32_t reserved1 = 0;
    uint32_t reserved2 = 0;
    std::vector<BinaryRelocation> relocations;

    std::vector<uint8_t> content(const std::vector<uint8_t> &image) const;

private:
    size_t header_offset = 0;
    friend class BinaryImage;
};

struct BinarySegment {
    std::string name;
    uint64_t virtual_address = 0;
    uint64_t virtual_size = 0;
    uint64_t file_offset = 0;
    uint64_t file_size = 0;
    uint64_t alignment = 0x1000;
    uint32_t max_protection = 0;
    uint32_t init_protection = 0;

private:
    size_t header_offset = 0;
    friend class BinaryImage;
};

class BinaryImage {
public:
    static std::unique_ptr<BinaryImage> parse(const std::filesystem::path &path);

    BinaryFormat format() const { return format_; }
    bool is_macho() const { return format_ == BinaryFormat::MachO; }
    bool is_elf() const { return format_ == BinaryFormat::ELF; }
    uint64_t entrypoint() const { return entrypoint_; }

    const std::vector<uint8_t> &data() const { return data_; }
    std::vector<BinarySection> &sections() { return sections_; }
    const std::vector<BinarySection> &sections() const { return sections_; }
    std::vector<BinarySegment> &segments() { return segments_; }
    const std::vector<BinarySegment> &segments() const { return segments_; }
    const std::vector<BinarySymbol> &symbols() const { return symbols_; }
    const std::vector<BinaryImport> &imports() const { return imports_; }

    BinarySection *section(const std::string &name);
    const BinarySection *section(const std::string &name) const;
    std::optional<uint64_t> virtual_address_to_offset(uint64_t va) const;
    std::optional<uint64_t> offset_to_virtual_address(uint64_t offset) const;
    std::vector<uint8_t> content_from_virtual_address(uint64_t va, size_t size) const;
    const BinarySymbol *symbol(size_t index) const;
    const std::string *indirect_symbol(size_t index) const;
    std::optional<uint64_t> symbol_address(const std::string &name) const;
    std::optional<uint64_t> import_slot(const std::string &name) const;
    std::optional<uint64_t> import_stub(const std::string &name) const;

    void add_executable_section(const std::string &name, int size,
                                const std::vector<uint8_t> &content);
    void write(const std::filesystem::path &path) const;

private:
    struct FatSlice {
        int32_t cpu_type = 0;
        int32_t cpu_subtype = 0;
        uint32_t alignment = 0;
        uint32_t reserved = 0;
        std::vector<uint8_t> data;
    };

    bool parse_macho();
    bool parse_elf();
    void parse_macho_tables(uint32_t symoff, uint32_t nsyms,
                            uint32_t stroff, uint32_t strsize,
                            uint32_t indirectoff, uint32_t nindirect);
    void parse_elf_dynamic(uint64_t dynamic_offset, uint64_t dynamic_size);
    void add_macho_section(const std::string &name, int size,
                           const std::vector<uint8_t> &content);
    void add_elf_section(const std::string &name, int size,
                         const std::vector<uint8_t> &content);
    void update_existing_section(BinarySection &section, int size,
                                 const std::vector<uint8_t> &content);
    std::vector<uint8_t> serialized() const;

    BinaryFormat format_ = BinaryFormat::MachO;
    std::vector<uint8_t> data_;
    std::vector<BinarySection> sections_;
    std::vector<BinarySegment> segments_;
    std::vector<BinarySymbol> symbols_;
    std::vector<std::string> indirect_symbols_;
    std::vector<BinaryImport> imports_;
    uint64_t entrypoint_ = 0;

    bool fat_ = false;
    bool fat64_ = false;
    size_t selected_slice_ = 0;
    std::vector<FatSlice> fat_slices_;
};
