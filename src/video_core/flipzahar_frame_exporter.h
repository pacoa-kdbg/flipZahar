// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include <string>
#include "common/common_types.h"

namespace VideoCore {

enum class FlipZaharFrameFormat : u32 {
    BGRA8 = 1,
    RGBA8 = 2,
};

#pragma pack(push, 1)
struct FlipZaharFrameHeader {
    char magic[8];
    u32 version;
    u32 header_size;
    u32 width;
    u32 height;
    u32 stride;
    u32 format;
    u64 frame_index;
    u64 monotonic_ns;
};
#pragma pack(pop)
static_assert(sizeof(FlipZaharFrameHeader) == 48);

class FlipZaharFrameExporter {
public:
    explicit FlipZaharFrameExporter(std::string path = {});

    [[nodiscard]] bool IsEnabled() const noexcept;

    [[nodiscard]] bool ExportFrame(u32 width, u32 height, u32 stride, FlipZaharFrameFormat format,
                                   std::span<const u8> pixels);

private:
    std::string path;
    u64 next_frame_index = 1;
};

} // namespace VideoCore
