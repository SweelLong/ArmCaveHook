#include "compiler.h"
#include "symbols.h"
#include <cstdio>
#include <cstring>
#include <regex>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <stdexcept>

static std::filesystem::path project_root() {
#ifdef ARMCAVE_PROJECT_ROOT
    return std::filesystem::path(ARMCAVE_PROJECT_ROOT);
#else
    return std::filesystem::path(__FILE__).parent_path().parent_path();
#endif
}

static std::string tempdir(const char *prefix) {
    static std::atomic<unsigned long long> counter{0};
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path(ec);
    if (ec)
        throw std::runtime_error("cannot determine temporary directory");

    for (int attempt = 0; attempt < 100; ++attempt) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto name = std::string(prefix) + std::to_string(stamp) + "_" +
                    std::to_string(counter.fetch_add(1));
        auto path = base / name;
        if (std::filesystem::create_directory(path, ec))
            return path.string();
        ec.clear();
    }
    throw std::runtime_error("cannot create temporary directory");
}

static std::string shell_quote(const std::string &value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += '"';
    return out;
#else
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += '\'';
    return out;
#endif
}

static std::string clang_driver(bool cxx) {
    return cxx ? "clang++" : "clang";
}

static std::string quiet_redirect() {
#ifdef _WIN32
    return " >NUL 2>NUL";
#else
    return " >/dev/null 2>/dev/null";
#endif
}

MachO open_macho(const std::string &path) {
    MachO mo;
    auto binary = BinaryImage::parse(path);
    if (binary && binary->is_macho()) mo.bin = std::move(binary);
    return mo;
}

static uint64_t parse_int(const std::string &value) {
    auto s = value;
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(0, 1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.empty() || s == "0" || s == "entry") return 0;
    return strtoull(s.c_str(), nullptr, 0);
}

static std::vector<HookAction> parse_meta(const std::vector<uint8_t> &data) {
    std::vector<HookAction> items;
    std::string all((const char *)data.data(), data.size());
    size_t pos = 0;
    while (true) {
        auto nul = all.find('\0', pos);
        if (nul == std::string::npos) break;
        std::string text = all.substr(pos, nul - pos);
        pos = nul + 1;
        while (!text.empty() && (unsigned char)text.back() <= 32) text.pop_back();
        auto bar = text.find('|');
        if (bar == std::string::npos) continue;
        HookAction act;
        act.kind = text.substr(0, bar);
        std::string rest = text.substr(bar + 1);
        size_t start = 0;
        while (true) {
            auto sep = rest.find('|', start);
            std::string part = rest.substr(start, sep - start);
            auto eq = part.find('=');
            if (eq != std::string::npos) {
                std::string k = part.substr(0, eq);
                std::string v = part.substr(eq + 1);
                if (k == "addr") act.address = (uint64_t)parse_int(v);
                else if (k == "handler") act.handler = v;
                else if (k == "segment") act.segment = v;
                else if (k == "size") act.size = atoi(v.c_str());
                else if (k == "expected") {
                    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                        v = v.substr(1, v.size() - 2);
                    act.expected = v;
                    act.has_expected = true;
                }
                else if (k == "data") act.data = v;
                else if (k == "regs") {
                    size_t rs = 0;
                    while (true) {
                        auto comma = v.find(',', rs);
                        std::string r = v.substr(rs, comma - rs);
                        while (!r.empty() && r[0] == ' ') r.erase(0, 1);
                        while (!r.empty() && r.back() == ' ') r.pop_back();
                        if (!r.empty()) {
                            for (auto &c : r) c = tolower(c);
                            act.register_args.push_back(r);
                        }
                        if (comma == std::string::npos) break;
                        rs = comma + 1;
                    }
                }
            }
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
        items.push_back(act);
    }
    return items;
}

static std::string init_segment(const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string line;
    std::regex re(R"(^\s*#\s*define\s+SEGMENT_NAME\s+([A-Za-z_][A-Za-z0-9_]*)\s*$)");
    while (std::getline(f, line)) {
        std::smatch m;
        if (std::regex_search(line, m, re))
            return m[1];
    }
    return {};
}

static std::map<std::string, int> symbol_offsets(BinaryImage *obj, BinarySection *text_sec) {
    std::map<std::string, int> out;
    uint64_t base = text_sec->virtual_address;
    uint64_t end = base + text_sec->size;
    for (auto &sym : obj->symbols()) {
        auto name = sym.name;
        if (name.empty()) continue;
        uint64_t val = sym.value;
        if (base <= val && val < end) {
            int off = (int)(val - base);
            out[name] = off;
            if (name[0] == '_') out[name.substr(1)] = off;
        } else if (val < text_sec->size) {
            int off = (int)val;
            out[name] = off;
            if (name[0] == '_') out[name.substr(1)] = off;
        }
    }
    return out;
}

static std::vector<uint8_t> extract_cave_asm() {
    std::string td = tempdir("armcave-");
    auto src = std::filesystem::path(td) / "c.cpp";
    auto out = std::filesystem::path(td) / "c.o";
    {
        std::ofstream f(src);
        f << "#include \"armcave.h\"\n";
    }
    std::string cmd = shell_quote(clang_driver(true)) + " -target arm64-apple-macosx13.0 -c -Oz -fno-stack-protector "
                      "-std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics "
                      "-I" + shell_quote((project_root() / "include").string()) + " "
                      + shell_quote(src.string()) + " -o " + shell_quote(out.string()) + quiet_redirect();
    int rc = system(cmd.c_str());
    (void)rc;
    auto mo = open_macho(out.string());
    if (!mo.bin) return {};
    auto *sec = mo.section("__caveasm");
    if (!sec || sec->size < 16) return {};
    return sec->content(mo.bin->data());
}

std::vector<uint8_t> extract_cave_asm_save() {
    auto all = extract_cave_asm();
    if (all.size() < 8) return {};
    return std::vector<uint8_t>(all.begin(), all.begin() + 8);
}

std::vector<uint8_t> extract_cave_asm_restore() {
    auto all = extract_cave_asm();
    if (all.size() < 12) return {};
    return std::vector<uint8_t>(all.begin() + 8, all.begin() + 12);
}

std::vector<uint8_t> extract_cave_asm_ret() {
    auto all = extract_cave_asm();
    if (all.size() < 16) return {};
    return std::vector<uint8_t>(all.begin() + 12, all.begin() + 16);
}

std::vector<uint8_t> assemble_aarch64(const std::string &source) {
    std::string td = tempdir("armcave-asm-");
    auto src = std::filesystem::path(td) / "a.s";
    auto out = std::filesystem::path(td) / "a.o";
    {
        std::ofstream f(src);
        f << ".text\n" << source << "\n";
    }
    std::string cmd = shell_quote(clang_driver(false)) +
                      " -target arm64-apple-macosx13.0 -c " + shell_quote(src.string()) +
                      " -o " + shell_quote(out.string()) + quiet_redirect();
    int rc = system(cmd.c_str());
    (void)rc;
    auto mo = open_macho(out.string());
    if (!mo.bin) return {};
    auto *sec = mo.section("__text");
    if (!sec) return {};
    return sec->content(mo.bin->data());
}

PluginBlob compile_plugin(const std::filesystem::path &path,
                          const std::filesystem::path *target_binary) {
    PluginBlob blob;
    std::string td = tempdir("armcave-");
    auto out = std::filesystem::path(td) / (path.stem().string() + ".o");
    if (path.extension() != ".cpp")
        throw std::runtime_error("plugin must be a .cpp file: " + path.string());

    std::string cmd = shell_quote(clang_driver(true)) + " -target arm64-apple-macosx13.0 -c -Oz -fno-stack-protector "
                      "-std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics "
                      "-I" + shell_quote((project_root() / "include").string());
    bool target_is_elf = false;
    if (target_binary) {
        std::ifstream target(*target_binary, std::ios::binary);
        unsigned char magic[4] = {};
        target.read(reinterpret_cast<char *>(magic), sizeof(magic));
        target_is_elf = target.gcount() == static_cast<std::streamsize>(sizeof(magic)) &&
                        magic[0] == 0x7f && magic[1] == 'E' &&
                        magic[2] == 'L' && magic[3] == 'F';
    }
    if (target_is_elf)
        cmd += " -DARMCAVE_ELF=1";
    if (!target_binary)
        cmd += " -ffreestanding -fno-builtin";
    cmd += " " + shell_quote(path.string()) + " -o " + shell_quote(out.string()) + quiet_redirect();
    int rc = system(cmd.c_str());
    if (rc != 0) {
        cmd = shell_quote(clang_driver(true)) + " -target arm64-apple-macosx13.0 -c -Oz -fno-stack-protector "
              "-std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics "
              "-I" + shell_quote((project_root() / "include").string());
        if (target_is_elf)
            cmd += " -DARMCAVE_ELF=1";
        if (!target_binary)
            cmd += " -ffreestanding -fno-builtin";
        cmd += " " + shell_quote(path.string()) + " -o " + shell_quote(out.string());
        rc = system(cmd.c_str());
        if (rc != 0)
            throw std::runtime_error("clang++ failed for " + path.string());
    }

    auto mo = open_macho(out.string());
    if (!mo.bin)
        throw std::runtime_error("failed to parse object: " + path.string());

    auto *text_sec = mo.section("__text");
    if (!text_sec)
        throw std::runtime_error("missing __text: " + path.string());

    {
        blob.text = text_sec->content(mo.bin->data());
    }

    std::vector<uint8_t> extra;
    for (auto &sec : mo.bin->sections()) {
        auto name = sec.name;
        if (name == "__text" || name == "__compact_unwind" ||
            name == "__eh_frame" || name == "__armhook" || name == "__armkeep")
            continue;
        if (name == "__caveasm" && target_is_elf)
            continue;
        if (name.size() >= 2 && name[0] == '_' && name[1] == '_') {
            while (extra.size() % 8 != 0) extra.push_back(0);
            blob.section_offsets[name] = (int)extra.size();
            auto content = sec.content(mo.bin->data());
            extra.insert(extra.end(), content.begin(), content.end());
        }
    }
    while (extra.size() % 16 != 0) extra.push_back(0);
    blob.extra = extra;

    for (auto &reloc : text_sec->relocations) {
        RelocEntry r;
        r.type = reloc.type;
        uint64_t address = reloc.address;
        if (address >= text_sec->size)
            throw std::runtime_error("text relocation is outside __text");
        r.address = (int)address;
        const BinarySymbol *sym = reloc.external ? mo.bin->symbol(reloc.symbol_index) : nullptr;
        if (sym) {
            r.symbol_name = sym->name;
            r.symbol_value = sym->value;
            if (!sym->undefined()) {
                for (auto &sec : mo.bin->sections()) {
                    uint64_t start = sec.virtual_address;
                    uint64_t end = start + sec.size;
                    if (start <= r.symbol_value && r.symbol_value < end) {
                        r.symbol_section = sec.name;
                        r.symbol_value -= start;
                        break;
                    }
                }
            }
        } else if (reloc.symbol_index > 0 && reloc.symbol_index <= mo.bin->sections().size()) {
            auto &sec = mo.bin->sections()[reloc.symbol_index - 1];
            r.symbol_section = sec.name;
            r.symbol_value = 0;
        }
        blob.relocs.push_back(r);
    }

    auto *meta_sec = mo.section("__armhook");
    if (meta_sec)
        blob.declarations = parse_meta(meta_sec->content(mo.bin->data()));

    blob.symbol_offsets = symbol_offsets(mo.bin.get(), text_sec);
    blob.default_segment = init_segment(path);

    return blob;
}

int PluginBlob::total_bytes() const {
    if (extra.empty())
        return (int)text.size();
    int aligned = ((int)text.size() + 15) & ~15;
    return aligned + (int)extra.size();
}

PluginBlob PluginBlob::for_action(const HookAction &action) const {
    PluginBlob b = *this;
    b.register_args = action.register_args;
    auto it = symbol_offsets.find(action.handler);
    if (it == symbol_offsets.end()) {
        std::string us = "_" + action.handler;
        it = symbol_offsets.find(us);
    }
    b.entry_offset = (it != symbol_offsets.end()) ? it->second : 0;
    return b;
}

std::vector<uint8_t> PluginBlob::build(uint64_t text_va, uint64_t data_va,
                                        const std::filesystem::path *target_binary) const {
    if (!target_binary || relocs.empty()) {
        std::vector<uint8_t> out = text;
        if (!extra.empty()) {
            while (out.size() % 16 != 0) out.push_back(0);
            out.insert(out.end(), extra.begin(), extra.end());
        }
        return out;
    }
    auto [new_text, new_extra] = resolve_plugin_relocs(
        text, extra, relocs, section_offsets,
        *target_binary, text_va, data_va);
    std::vector<uint8_t> out = new_text;
    if (!new_extra.empty()) {
        while (out.size() % 16 != 0) out.push_back(0);
        out.insert(out.end(), new_extra.begin(), new_extra.end());
    }
    return out;
}
