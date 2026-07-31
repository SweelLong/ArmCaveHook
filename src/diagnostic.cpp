#include "diagnostic.h"

#include <cstdio>

namespace {

std::string json_escape(const std::string &value) {
    std::string out;
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

void emit(const char *level, const std::string &stage,
          const std::string &message, uint64_t address,
          const std::string &type, const std::string &context) {
    std::fprintf(stderr,
                 "{\"level\":\"%s\",\"stage\":\"%s\",\"message\":\"%s\"",
                 level, json_escape(stage).c_str(), json_escape(message).c_str());
    if (address)
        std::fprintf(stderr, ",\"address\":\"0x%llx\"",
                     (unsigned long long)address);
    if (!type.empty())
        std::fprintf(stderr, ",\"type\":\"%s\"", json_escape(type).c_str());
    if (!context.empty())
        std::fprintf(stderr, ",\"context\":\"%s\"",
                     json_escape(context).c_str());
    std::fputs("}\n", stderr);
}

}

void diagnostic_error(const std::string &stage, const std::string &message,
                      uint64_t address, const std::string &type,
                      const std::string &context) {
    emit("error", stage, message, address, type, context);
}

void diagnostic_warning(const std::string &stage, const std::string &message,
                        uint64_t address, const std::string &type,
                        const std::string &context) {
    emit("warning", stage, message, address, type, context);
}
