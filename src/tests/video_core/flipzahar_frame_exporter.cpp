// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "video_core/flipzahar_frame_exporter.h"

namespace {

std::vector<u8> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("FlipZahar frame exporter writes header and pixels atomically", "[video_core]") {
    const auto output_path = std::filesystem::temp_directory_path() / "flipzahar-frame-export-test.fzf";
    std::filesystem::remove(output_path);

    const std::array<u8, 16> pixels{
        0x10, 0x20, 0x30, 0x40,
        0x50, 0x60, 0x70, 0x80,
        0x90, 0xA0, 0xB0, 0xC0,
        0xD0, 0xE0, 0xF0, 0x00,
    };

    VideoCore::FlipZaharFrameExporter exporter(output_path.string());

    REQUIRE(exporter.IsEnabled());
    REQUIRE(exporter.ExportFrame(2, 2, 8, VideoCore::FlipZaharFrameFormat::BGRA8,
                                 std::span<const u8>{pixels}));

    const auto bytes = ReadBinaryFile(output_path);
    REQUIRE(bytes.size() == sizeof(VideoCore::FlipZaharFrameHeader) + pixels.size());

    VideoCore::FlipZaharFrameHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));

    REQUIRE(std::string_view{header.magic, 8} == std::string_view{"FZFRAME\0", 8});
    REQUIRE(header.version == 1);
    REQUIRE(header.header_size == sizeof(VideoCore::FlipZaharFrameHeader));
    REQUIRE(header.width == 2);
    REQUIRE(header.height == 2);
    REQUIRE(header.stride == 8);
    REQUIRE(header.format == static_cast<u32>(VideoCore::FlipZaharFrameFormat::BGRA8));
    REQUIRE(header.frame_index == 1);
    REQUIRE(header.monotonic_ns > 0);

    REQUIRE(std::equal(pixels.begin(), pixels.end(), bytes.begin() + sizeof(header)));

    std::filesystem::remove(output_path);
}

TEST_CASE("FlipZahar frame exporter rejects short pixel buffers", "[video_core]") {
    const auto output_path = std::filesystem::temp_directory_path() / "flipzahar-frame-export-short.fzf";
    std::filesystem::remove(output_path);

    const std::array<u8, 7> pixels{};
    VideoCore::FlipZaharFrameExporter exporter(output_path.string());

    REQUIRE_FALSE(exporter.ExportFrame(2, 2, 4, VideoCore::FlipZaharFrameFormat::BGRA8,
                                       std::span<const u8>{pixels}));
    REQUIRE_FALSE(std::filesystem::exists(output_path));
}
