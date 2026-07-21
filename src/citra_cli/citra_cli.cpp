// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string_view>

#include "citra_cli/citra_cli.h"
#include "citra_cli/compression_cli.h"

namespace CitraCLI {

namespace {

bool OptStringContainsOption(const char* optstring, char option) {
    for (std::size_t i = 0; optstring[i] != '\0'; ++i) {
        if (optstring[i] == ':') {
            continue;
        }
        if (optstring[i] == option) {
            return true;
        }
    }
    return false;
}

} // namespace

bool CheckForOptions(const char* optstring, int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--") {
            return false;
        }
        if (!arg.starts_with('-') || arg.size() < 2) {
            continue;
        }
        if (arg.starts_with("--")) {
            continue;
        }

        for (std::size_t j = 1; j < arg.size(); ++j) {
            if (OptStringContainsOption(optstring, arg[j])) {
                return true;
            }
        }
    }

    return false;
}

int ParseCommand(int argc, char* argv[]) {
    if (CheckForOptions(compression_ops_optstring, argc, argv)) {
        return ParseCompressionCommand(argc, argv);
    }
    return 0;
}

} // namespace CitraCLI
