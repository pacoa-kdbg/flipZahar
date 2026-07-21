# flipZahar implementation progress

## 2026-07-20 Vulkan secondary-frame export MVP

Goal: replace the OpenGL/X11 screenshot bridge with a first correctness-oriented renderer export path for the Azahar secondary/bottom Vulkan window.

### Implemented locally

- Added backend-neutral raw frame exporter:
  - `src/video_core/flipzahar_frame_exporter.h`
  - `src/video_core/flipzahar_frame_exporter.cpp`
- Added Catch2 coverage for header/pixel serialization and short-buffer rejection:
  - `src/tests/video_core/flipzahar_frame_exporter.cpp`
- Registered exporter source/test in:
  - `src/video_core/CMakeLists.txt`
  - `src/tests/CMakeLists.txt`
- Wired Vulkan secondary-window export in:
  - `src/video_core/renderer_vulkan/renderer_vulkan.cpp`
  - `src/video_core/renderer_vulkan/renderer_vulkan.h`

### Runtime handoff

The existing CLI plumbing sets:

```text
FLIPZAHAR_BOTTOM_FRAME_EXPORT=<path>
```

When the Vulkan renderer sees that env var and renders the secondary window, it now:

1. draws the secondary frame normally,
2. copies the secondary `Frame::image` to a host-visible staging buffer,
3. blocks with `scheduler.Finish()` for the MVP,
4. writes a raw `FZFRAME` file atomically via `<path>.tmp` + rename.

### Raw file format

```c
struct FlipZaharFrameHeader {
    char magic[8];       // "FZFRAME\0"
    uint32_t version;    // 1
    uint32_t header_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;     // 1 BGRA8, 2 RGBA8
    uint64_t frame_index;
    uint64_t monotonic_ns;
};
// followed by stride * height bytes
```

### Verification so far

Local/Azure VM:

- `git diff --check`: passed.
- Focused object compile passed:
  - `src/video_core/flipzahar_frame_exporter.cpp`
  - `src/tests/video_core/flipzahar_frame_exporter.cpp`
  - `src/citra_cli/citra_cli.cpp`
- Full local test build is not practical on the VM; it is ARM64 and was nearly full on disk during this work.

Flip DS 1S:

- Synced working tree to `/home/deck/src/flipZahar-work` with `rsync` excluding `.git` and build dirs.
- Reconfigured `build-flipzahar-verify` with SDL2 enabled and LTO disabled for a proper Qt link path.
- Built full binary successfully:
  - `build-flipzahar-verify/bin/RelWithDebInfo/azahar`
- Verified `--help` under Qt offscreen/minimal/xcb includes:
  - `--separate-windows`
  - `--flipds-dual-screen`
  - `--graphics-api [api]`
  - `--flipds-bottom-frame-export [path]`
- Found and fixed a launch blocker: `CitraCLI::CheckForOptions()` used `getopt()` to scan for compression flags and misinterpreted long Qt/fork flags such as `--graphics-api` as `-c`/`-x` compression operations. The scanner now ignores long options and only matches explicit short options.
- Safe homebrew smoke test used only:
  - `/home/deck/Applications/azahar-dual-test/3ds-app-test.3dsx`
- Runtime command produced a real exported Vulkan frame:

```text
/tmp/flipzahar-bottom-frame.fzf
size: 8,294,448 bytes
header: magic=FZFRAME\0 version=1 header_size=48 width=1920 height=1080 stride=7680 format=1 frame_index=1443
expected size: 48 + 7680 * 1080 = 8,294,448
nonzero payload bytes: 6,660,796
unique payload byte values: 206
```

- Converted exported payload to `/tmp/flipzahar-bottom-frame-latest.png`; it visibly matches the expected `3ds-app-test` bottom menu (`first menu`, `2nd menu`, `3rd menu`, `...`). This proves the export is the actual bottom screen, not a black Vulkan/XID capture.
- Smoke test was terminated after export verification; no stale `azahar` process remained.

### Known caveats / next steps

- This is intentionally synchronous and may stall every exported frame. Replace with persistent/ringed staging buffers before performance work.
- The consumer side still needs update from PPM polling to `FZFRAME` parsing.
- The exported frame is the secondary window size (`1920x1080` in the gamescope/Xwayland session), with the 3DS bottom menu scaled inside it. The lease consumer should crop/scale/rotate for DP-1.
- Do not test real games/saves until the homebrew export path is integrated into the lease renderer and verified visually on DP-1.
