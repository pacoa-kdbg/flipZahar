// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/flipzahar_frame_exporter.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"

namespace VideoCore {
namespace {

constexpr char FrameMagic[8] = {'F', 'Z', 'F', 'R', 'A', 'M', 'E', '\0'};

[[nodiscard]] u64 GetMonotonicNanoseconds() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace

FlipZaharFrameExporter::FlipZaharFrameExporter(std::string path_) : path{std::move(path_)} {}

bool FlipZaharFrameExporter::IsEnabled() const noexcept {
    return !path.empty();
}

bool FlipZaharFrameExporter::ExportFrame(u32 width, u32 height, u32 stride,
                                         FlipZaharFrameFormat format, std::span<const u8> pixels) {
    if (!IsEnabled()) {
        return false;
    }

    const u64 expected_size = static_cast<u64>(stride) * height;
    if (width == 0 || height == 0 || stride < width * 4 || pixels.size_bytes() < expected_size) {
        LOG_ERROR(Render, "FlipZahar frame export rejected invalid frame {}x{} stride={} bytes={}",
                  width, height, stride, pixels.size_bytes());
        return false;
    }

    const FlipZaharFrameHeader header{
        .magic = {FrameMagic[0], FrameMagic[1], FrameMagic[2], FrameMagic[3], FrameMagic[4],
                  FrameMagic[5], FrameMagic[6], FrameMagic[7]},
        .version = 1,
        .header_size = sizeof(FlipZaharFrameHeader),
        .width = width,
        .height = height,
        .stride = stride,
        .format = static_cast<u32>(format),
        .frame_index = next_frame_index++,
        .monotonic_ns = GetMonotonicNanoseconds(),
    };

    const std::filesystem::path output_path{path};
    const auto parent_path = output_path.parent_path();
    if (!parent_path.empty() && !FileUtil::CreateFullPath(parent_path.string() + DIR_SEP)) {
        LOG_ERROR(Render, "FlipZahar frame export failed to create {}", parent_path.string());
        return false;
    }

    const std::string temp_path = path + ".tmp";
    FileUtil::IOFile file(temp_path, "wb");
    if (!file.IsOpen()) {
        LOG_ERROR(Render, "FlipZahar frame export failed to open {}", temp_path);
        return false;
    }

    const bool write_ok = file.WriteObject(header) == 1 &&
                          file.WriteBytes(pixels.data(), static_cast<std::size_t>(expected_size)) ==
                              expected_size &&
                          file.Flush();
    if (!file.Close() || !write_ok) {
        LOG_ERROR(Render, "FlipZahar frame export failed while writing {}", temp_path);
        FileUtil::Delete(temp_path);
        return false;
    }

    if (!FileUtil::Rename(temp_path, path)) {
        LOG_ERROR(Render, "FlipZahar frame export failed to publish {}", path);
        FileUtil::Delete(temp_path);
        return false;
    }

    return true;
}

} // namespace VideoCore
