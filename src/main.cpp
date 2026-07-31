#include "pipeline.h"
#include "diagnostic.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>
#include <string>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <input> [-o <output>] [--plugins <dir>]\n"
        "       %s --script <patch.toml>\n"
        "       [--plugin-whitelist <names>] [--plugin-blacklist <names>]\n",
        prog, prog);
}

int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(argv[0]);
        return 0;
    }
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (argc >= 2 && (!strcmp(argv[1], "--script") ||
                      std::filesystem::path(argv[1]).extension() == ".toml")) {
        std::filesystem::path script_path = !strcmp(argv[1], "--script")
            ? (argc >= 3 ? argv[2] : std::filesystem::path())
            : argv[1];
        if (script_path.empty()) {
            usage(argv[0]);
            return 1;
        }
        try {
            run_patch_script(script_path);
        } catch (std::exception &e) {
            diagnostic_error("patch_script", e.what());
            return 1;
        }
        return 0;
    }

    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::filesystem::path plugins_dir;
    std::string whitelist, blacklist;

    input_path = argv[1];

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (!strcmp(argv[i], "--plugins") && i + 1 < argc) {
            plugins_dir = argv[++i];
        } else if (!strcmp(argv[i], "--plugin-whitelist") && i + 1 < argc) {
            whitelist = argv[++i];
        } else if (!strcmp(argv[i], "--plugin-blacklist") && i + 1 < argc) {
            blacklist = argv[++i];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    try {
        run_pipeline(input_path, output_path, plugins_dir, nullptr,
                     whitelist.empty() ? nullptr : &whitelist,
                     blacklist.empty() ? nullptr : &blacklist);
    } catch (std::exception &e) {
        diagnostic_error("pipeline", e.what());
        return 1;
    }

    return 0;
}
