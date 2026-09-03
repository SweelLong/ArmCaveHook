#include "compiler.h"
#include "symbols.h"
#include "aarch64/encoder.h"
#include <cstdio>
#include <cstring>
#include <regex>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <cctype>

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

static std::string compiler_error_redirect(const std::filesystem::path &path) {
#ifdef _WIN32
    return " >NUL 2>" + shell_quote(path.string());
#else
    return " >/dev/null 2>" + shell_quote(path.string());
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

static std::string unescape_metadata(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out += value[i];
            continue;
        }
        char escaped = value[++i];
        switch (escaped) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        default:
            out += '\\';
            out += escaped;
            break;
        }
    }
    return out;
}

static std::string trim_text(std::string value) {
    while (!value.empty() && std::isspace((unsigned char)value.front()))
        value.erase(value.begin());
    while (!value.empty() && std::isspace((unsigned char)value.back()))
        value.pop_back();
    return value;
}

static std::string unquote_metadata(std::string value) {
    value = trim_text(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
    return unescape_metadata(value);
}

static std::vector<std::string> parse_asm_lines(std::string value) {
    value = trim_text(value);
    if (value.empty()) return {};

    if (value.front() != '[')
        return {unquote_metadata(value)};

    std::vector<std::string> lines;
    bool saw_string = false;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '"') continue;
        saw_string = true;
        std::string line;
        for (++i; i < value.size(); ++i) {
            if (value[i] == '"') break;
            if (value[i] == '\\' && i + 1 < value.size()) {
                char escaped = value[++i];
                switch (escaped) {
                case 'n': line += '\n'; break;
                case 'r': line += '\r'; break;
                case 't': line += '\t'; break;
                case '\\': line += '\\'; break;
                case '"': line += '"'; break;
                default: line += escaped; break;
                }
            } else {
                line += value[i];
            }
        }
        lines.push_back(std::move(line));
    }
    if (saw_string) return lines;
    return {unquote_metadata(value)};
}

static std::string split_asm_statements(const std::vector<std::string> &lines) {
    std::string source;
    bool quoted = false;
    bool escaped = false;
    for (const auto &line : lines) {
        for (char c : line) {
            if (escaped) {
                source += c;
                escaped = false;
                continue;
            }
            if (c == '\\' && quoted) {
                source += c;
                escaped = true;
                continue;
            }
            if (c == '"') {
                quoted = !quoted;
                source += c;
                continue;
            }
            if (c == ';' && !quoted)
                source += '\n';
            else
                source += c;
        }
        source += '\n';
    }
    if (!source.empty()) source.pop_back();
    return source;
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
                if (k == "addr") {
                    act.address = (uint64_t)parse_int(v);
                }
                else if (k == "args" && act.kind == "new_asm_func") act.data = v;
                else if (k == "signature") act.signature = v;
                else if (k == "symbol") act.symbol = v;
                else if (k == "objc_class") act.objc_class = v;
                else if (k == "selector") act.selector = v;
                else if (k == "swift") act.swift_name = v;
                else if (k == "handler") act.handler = v;
                else if (k == "name" && (act.kind == "new_asm_func" ||
                                          act.kind == "new_cpp_func"))
                    act.function_name = v;
                else if (k == "segment") act.segment = v;
                else if (k == "size") act.size = atoi(v.c_str());
                else if (k == "expected") {
                    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                        v = v.substr(1, v.size() - 2);
                    act.expected = unescape_metadata(v);
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

static std::map<std::string, int> data_symbol_offsets(
    BinaryImage *obj, const std::map<std::string, int> &section_offsets) {
    std::map<std::string, int> out;
    const auto &sections = obj->sections();
    for (const auto &sym : obj->symbols()) {
        if (sym.name.empty() || sym.undefined() || sym.section_index == 0 ||
            sym.section_index > sections.size())
            continue;
        const auto &section = sections[sym.section_index - 1];
        auto section_offset = section_offsets.find(section.name);
        if (section_offset == section_offsets.end())
            continue;
        uint64_t relative = sym.value;
        if (section.virtual_address <= sym.value &&
            sym.value < section.virtual_address + section.size)
            relative = sym.value - section.virtual_address;
        if (relative >= section.size)
            continue;
        int offset = section_offset->second + (int)relative;
        out[sym.name] = offset;
        if (sym.name[0] == '_') out[sym.name.substr(1)] = offset;
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

static std::string normalize_absolute_branches(const std::string &source,
                                               uint64_t address) {
    static const std::regex adrl_re(
        R"(^([ \t]*)adrl[ \t]+(x[0-9]+)[ \t]*,[ \t]*#?([A-Za-z_.$][A-Za-z0-9_.$]*)(.*)$)",
        std::regex::icase);
    static const std::regex branch_re(
        R"(^([ \t]*(?:[A-Za-z_][A-Za-z0-9_]*:[ \t]*)?)(b(?:\.[A-Za-z0-9]+)?|bl)[ \t]+(0[xX][0-9A-Fa-f]+|[0-9]+)(.*)$)",
        std::regex::icase);
    static const std::regex adrp_re(
        R"(^([ \t]*adrp[ \t]+x[0-9]+[ \t]*,[ \t]*#?[ \t]*)(0[xX][0-9A-Fa-f]+|[0-9]+)(.*)$)",
        std::regex::icase);
    static const std::regex compare_branch_re(
        R"(^([ \t]*(?:(?:cbz|cbnz)[ \t]+[wx][0-9]+[ \t]*,[ \t]*|(?:tbz|tbnz)[ \t]+[wx][0-9]+[ \t]*,[ \t]*#[0-9]+[ \t]*,[ \t]*))(0[xX][0-9A-Fa-f]+|[0-9]+)(.*)$)",
        std::regex::icase);
    std::istringstream input(source);
    std::ostringstream output;
    std::string line;
    uint64_t offset = 0;
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, adrl_re)) {
            // Darwin's AArch64 assembler does not accept the GNU/LLVM adrl
            // pseudo-instruction.  Expand it to the pair of Mach-O page
            // relocations used by adrp/add while retaining one source line.
            line = match[1].str() + "adrp " + match[2].str() + ", " +
                   match[3].str() + "@PAGE; add " + match[2].str() + ", " +
                   match[2].str() + ", " + match[3].str() + "@PAGEOFF" +
                   match[4].str();
        } else if (std::regex_match(line, match, branch_re)) {
            uint64_t target = 0;
            try {
                target = std::stoull(match[3].str(), nullptr, 0);
            } catch (...) {
                target = 0;
            }
            std::string mnemonic = match[2].str();
            for (char &c : mnemonic)
                c = (char)std::tolower((unsigned char)c);
            if (target >= 0x100000000ULL && !address &&
                (mnemonic == "b" || mnemonic == "bl")) {
                // Keep the target symbolic until the final plugin VA is known.
                // Encoding the absolute VA as MOVZ/MOVK is not ASLR-safe.
                std::ostringstream symbol;
                symbol << "armcave_absolute_" << std::hex << target;
                line = match[1].str() + match[2].str() + " " +
                       symbol.str() + match[4].str();
            } else if (target >= 0x100000000ULL) {
                int64_t pc = (int64_t)(address + offset);
                int64_t target_signed = (int64_t)target;
                int64_t relative = target_signed - pc;
                line = match[1].str() + match[2].str() + " " +
                       std::to_string(relative) + match[4].str();
            }
        } else if (std::regex_match(line, match, adrp_re)) {
            uint64_t target = 0;
            try {
                target = std::stoull(match[2].str(), nullptr, 0);
            } catch (...) {
                target = 0;
            }
            if (target >= 0x100000000ULL) {
                uint64_t pc = address + offset;
                int64_t relative = (int64_t)(target & ~0xfffULL) -
                                    (int64_t)(pc & ~0xfffULL);
                line = match[1].str() + std::to_string(relative) + match[3].str();
            }
        } else if (std::regex_match(line, match, compare_branch_re)) {
            uint64_t target = 0;
            try {
                target = std::stoull(match[2].str(), nullptr, 0);
            } catch (...) {
                target = 0;
            }
            if (target >= 0x100000000ULL) {
                int64_t pc = (int64_t)(address + offset);
                int64_t target_signed = (int64_t)target;
                int64_t relative = target_signed - pc;
                line = match[1].str() + std::to_string(relative) + match[3].str();
            }
        }
        output << line;
        if (!input.eof()) output << '\n';

        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (!trimmed.empty() && trimmed[0] != '.') {
            std::size_t start = 0;
            while (start < trimmed.size()) {
                std::size_t end = trimmed.find(';', start);
                std::string instruction = trimmed.substr(start, end - start);
                while (!instruction.empty() &&
                       (instruction.back() == ' ' || instruction.back() == '\t'))
                    instruction.pop_back();
                if (!instruction.empty() && instruction.back() != ':')
                    offset += 4;
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
        }
    }
    return output.str();
}

std::vector<uint8_t> assemble_aarch64(const std::string &source, uint64_t address) {
    std::string td = tempdir("armcave-asm-");
    auto src = std::filesystem::path(td) / "a.s";
    auto out = std::filesystem::path(td) / "a.o";
    auto error = std::filesystem::path(td) / "a.err";
    {
        std::ofstream f(src);
        f << ".text\n" << normalize_absolute_branches(source, address) << "\n";
    }
    std::string cmd = shell_quote(clang_driver(false)) +
                      " -target arm64-apple-macosx13.0 -c " + shell_quote(src.string()) +
                      " -o " + shell_quote(out.string()) + compiler_error_redirect(error);
    int rc = system(cmd.c_str());
    if (rc != 0) {
        std::ifstream input(error);
        std::string detail((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r'))
            detail.pop_back();
        if (detail.empty()) detail = "exit status " + std::to_string(rc);
        throw std::runtime_error("AArch64 assembler failed: " + detail);
    }
    auto mo = open_macho(out.string());
    if (!mo.bin)
        throw std::runtime_error("AArch64 assembler produced an invalid object");
    auto *sec = mo.section("__text");
    if (!sec)
        throw std::runtime_error("AArch64 assembler object has no __text section");
    auto payload = sec->content(mo.bin->data());
    if (payload.empty())
        throw std::runtime_error("AArch64 assembler produced an empty payload");
    return payload;
}

static MachO assemble_aarch64_object(const std::string &source) {
    std::string td = tempdir("armcave-asm-object-");
    auto src = std::filesystem::path(td) / "a.s";
    auto out = std::filesystem::path(td) / "a.o";
    auto error = std::filesystem::path(td) / "a.err";
    {
        std::ofstream f(src);
        f << ".text\n" << normalize_absolute_branches(source, 0) << "\n";
    }
    std::string cmd = shell_quote(clang_driver(false)) +
                      " -target arm64-apple-macosx13.0 -c " + shell_quote(src.string()) +
                      " -o " + shell_quote(out.string()) + compiler_error_redirect(error);
    if (system(cmd.c_str()) != 0) {
        std::ifstream input(error);
        std::string detail((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r'))
            detail.pop_back();
        if (detail.empty()) detail = "unknown assembler error";
        throw std::runtime_error("AArch64 assembler failed for new_asm_func: " + detail);
    }
    auto mo = open_macho(out.string());
    if (!mo.bin || !mo.section("__text"))
        throw std::runtime_error("AArch64 assembler produced an invalid new_asm_func object");
    return mo;
}

static void append_asm_text_relocations(PluginBlob &blob, BinaryImage *image,
                                        BinarySection *text_sec, int base,
                                        bool from_new_asm_func = false) {
    bool has_addend = false;
    int64_t pending_addend = 0;
    uint64_t pending_address = 0;
    for (auto &reloc : text_sec->relocations) {
        uint64_t address = reloc.address;
        if (address >= text_sec->size)
            throw std::runtime_error("text relocation is outside __text");
        if (reloc.type == 10) {
            int32_t raw = (int32_t)(reloc.symbol_index & 0x00ffffff);
            if (raw & 0x00800000) raw |= (int32_t)0xff000000;
            pending_addend = raw;
            pending_address = address;
            has_addend = true;
            continue;
        }
        RelocEntry r;
        r.type = reloc.type;
        r.address = base + (int)address;
        r.from_new_asm_func = from_new_asm_func;
        if (has_addend) {
            if (pending_address != address)
                throw std::runtime_error("ARM64_RELOC_ADDEND is not paired");
            r.addend = pending_addend;
            has_addend = false;
        }
        const BinarySymbol *sym = reloc.external ? image->symbol(reloc.symbol_index) : nullptr;
        if (sym) {
            r.symbol_name = sym->name;
            std::string normalized_name = r.symbol_name;
            if (!normalized_name.empty() && normalized_name[0] == '_')
                normalized_name.erase(0, 1);
            static const std::string absolute_prefix = "armcave_absolute_";
            if (from_new_asm_func &&
                normalized_name.compare(0, absolute_prefix.size(), absolute_prefix) == 0) {
                auto value = normalized_name.substr(absolute_prefix.size());
                if (value.empty())
                    throw std::runtime_error("new_asm_func has an empty absolute branch target");
                char *end = nullptr;
                uint64_t target = strtoull(value.c_str(), &end, 16);
                if (!end || *end != '\0')
                    throw std::runtime_error("new_asm_func has an invalid absolute branch target: " +
                                             value);
                r.has_absolute_target = true;
                r.absolute_target = target;
            }
            r.symbol_value = sym->value;
            if (!sym->undefined()) {
                for (auto &sec : image->sections()) {
                    uint64_t start = sec.virtual_address;
                    uint64_t end = start + sec.size;
                    if (start <= r.symbol_value && r.symbol_value < end) {
                        r.symbol_section = sec.name;
                        r.symbol_value -= start;
                        break;
                    }
                }
            }
        } else if (reloc.symbol_index > 0 && reloc.symbol_index <= image->sections().size()) {
            auto &sec = image->sections()[reloc.symbol_index - 1];
            r.symbol_section = sec.name;
        }
        blob.relocs.push_back(r);
    }
    if (has_addend)
        throw std::runtime_error("orphan ARM64_RELOC_ADDEND");
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
            if (sec.segment_name != "__TEXT")
                blob.has_writable_extra = true;
            while (extra.size() % 8 != 0) extra.push_back(0);
            blob.section_offsets[name] = (int)extra.size();
            auto content = sec.content(mo.bin->data());
            extra.insert(extra.end(), content.begin(), content.end());
        }
    }
    while (extra.size() % 16 != 0) extra.push_back(0);
    blob.extra = extra;
    blob.data_symbol_offsets = data_symbol_offsets(mo.bin.get(), blob.section_offsets);

    append_asm_text_relocations(blob, mo.bin.get(), text_sec, 0);

    auto *meta_sec = mo.section("__armhook");
    if (meta_sec)
        blob.declarations = parse_meta(meta_sec->content(mo.bin->data()));

    blob.symbol_offsets = symbol_offsets(mo.bin.get(), text_sec);
    blob.default_segment = init_segment(path);

    // Assembly functions are kept separate from C++ handlers so local labels
    // can be resolved by one assembler invocation per function.
    for (auto &action : blob.declarations) {
        if (action.kind != "new_asm_func") continue;
        if (action.function_name.empty())
            throw std::runtime_error("new_asm_func is missing its name in " +
                                     path.string());
        if (action.data.empty())
            throw std::runtime_error("new_asm_func requires an assembly body in " +
                                     path.string());
        auto lines = parse_asm_lines(action.data);
        if (lines.empty())
            throw std::runtime_error("new_asm_func has an empty body in " + path.string());
        std::string source = split_asm_statements(lines);
        auto asm_mo = assemble_aarch64_object(source);
        auto *asm_text = asm_mo.section("__text");
        auto bytes = asm_text->content(asm_mo.bin->data());
        while (blob.text.size() % 4 != 0) blob.text.push_back(0);
        action.asm_offset = (int)blob.text.size();
        append_asm_text_relocations(blob, asm_mo.bin.get(), asm_text, action.asm_offset,
                                    true);
        blob.text.insert(blob.text.end(), bytes.begin(), bytes.end());
        if (!blob.function_offsets.emplace(action.function_name,
                                           action.asm_offset).second)
            throw std::runtime_error("duplicate new function name in " + path.string());
    }

    for (const auto &action : blob.declarations) {
        if (action.kind != "new_cpp_func") continue;
        if (action.handler.empty())
            throw std::runtime_error("new_cpp_func is missing its handler in " + path.string());
        std::string function_name = action.function_name.empty()
            ? action.handler : action.function_name;
        auto it = blob.symbol_offsets.find(action.handler);
        if (it == blob.symbol_offsets.end()) {
            std::string us = "_" + action.handler;
            it = blob.symbol_offsets.find(us);
        }
        if (it == blob.symbol_offsets.end())
            throw std::runtime_error("handler symbol is not local to plugin: " +
                                     action.handler);
        if (!blob.function_offsets.emplace(function_name, it->second).second)
            throw std::runtime_error("duplicate new function name in " + path.string());
    }

    // Resolve references emitted by new_asm_func to registered C++ handlers.
    // They are initially undefined Mach-O branch relocations because the
    // assembler runs before the plugin segment receives its final VA.
    for (auto &reloc : blob.relocs) {
        if (!reloc.from_new_asm_func || reloc.type != 2) continue;
        if (reloc.has_absolute_target) continue;
        if (reloc.symbol_name.empty())
            throw std::runtime_error("new_asm_func contains a branch with no symbol");
        auto it = blob.function_offsets.find(reloc.symbol_name);
        if (it == blob.function_offsets.end() && reloc.symbol_name[0] == '_')
            it = blob.function_offsets.find(reloc.symbol_name.substr(1));
        if (it == blob.function_offsets.end() && reloc.symbol_name[0] != '_')
            it = blob.function_offsets.find("_" + reloc.symbol_name);
        if (it == blob.function_offsets.end())
            throw std::runtime_error("new_asm_func branch references unregistered function: " +
                                     reloc.symbol_name);
        reloc.symbol_section = "__text";
        reloc.symbol_value = (uint64_t)it->second;
    }

    return blob;
}

int PluginBlob::total_bytes() const {
    if (extra.empty())
        return max_text_bytes();
    int aligned = (max_text_bytes() + 15) & ~15;
    return aligned + (int)extra.size();
}

int PluginBlob::max_text_bytes() const {
    int veneers = 0;
    for (const auto &reloc : relocs)
        if (reloc.type == 2)
            veneers += (int)armcave::aarch64::kMaxBranchSequenceBytes;
    return (int)text.size() + veneers;
}

PluginBlob PluginBlob::for_action(const HookAction &action) const {
    PluginBlob b = *this;
    if (action.kind == "new_asm_func") {
        b.register_args.clear();
        b.entry_offset = action.asm_offset;
        return b;
    }
    b.register_args = action.register_args;
    auto it = symbol_offsets.find(action.handler);
    if (it == symbol_offsets.end()) {
        std::string us = "_" + action.handler;
        it = symbol_offsets.find(us);
    }
    if (it == symbol_offsets.end())
        throw std::runtime_error("handler symbol is not local to plugin: " +
                                 action.handler);
    b.entry_offset = it->second;
    return b;
}

std::vector<uint8_t> PluginBlob::build(uint64_t text_va, uint64_t data_va,
                                        const std::filesystem::path *target_binary) const {
    if (!target_binary || relocs.empty()) {
        std::vector<uint8_t> out = text;
        out.resize((size_t)max_text_bytes(), 0);
        if (!extra.empty()) {
            while (out.size() % 16 != 0) out.push_back(0);
            out.insert(out.end(), extra.begin(), extra.end());
        }
        return out;
    }
    auto [new_text, new_extra] = resolve_plugin_relocs(
        text, extra, relocs, section_offsets,
        *target_binary, text_va, data_va);
    new_text.resize((size_t)max_text_bytes(), 0);
    std::vector<uint8_t> out = new_text;
    if (!new_extra.empty()) {
        while (out.size() % 16 != 0) out.push_back(0);
        out.insert(out.end(), new_extra.begin(), new_extra.end());
    }
    return out;
}
