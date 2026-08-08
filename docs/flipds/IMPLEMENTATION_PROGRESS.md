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

## DRM-lease integration summary for implementers

### What this fork changes

The fork makes Azahar the **producer** in a gamescope DRM-lease pipeline. It does not create a DRM lease or submit scanout buffers itself.

The gamescope implementation used for this work is the [`mmogr/gamescope` DRM-lease fork](https://github.com/mmogr/gamescope), which provides the leased-connector path consumed by the external renderer.

1. `--flipds-dual-screen` (or `--separate-windows`) selects Azahar's existing Separate Windows layout and applies launch-local defaults: no screen swap/upright transform and no single-window mode. The main/top screen remains in the primary Azahar window; the bottom screen is rendered through the secondary window.
2. `--graphics-api software|opengl|vulkan` makes renderer selection launch-local, avoiding edits to Qt configuration when moving between prototype paths.
3. `--flipds-bottom-frame-export <path>` passes the caller-supplied path to the renderer as `FLIPZAHAR_BOTTOM_FRAME_EXPORT`.
4. On the Vulkan secondary-window render path, the renderer copies the completed `Frame::image` into a host-visible staging buffer and atomically writes an `FZFRAME` payload to the requested path.

This replaces the earlier OpenGL-only bridge:

```text
Azahar bottom X11 window -> screenshot/capture polling -> PPM -> lease renderer -> leased panel
```

That bridge proved lease ownership and physical routing, but cannot reliably capture Vulkan/gamescope WSI output (black captures). The intended path is:

```text
Azahar Vulkan secondary output -> FZFRAME -> lease consumer -> DRM lease scanout
```

### Consumer contract and remaining work

The lease consumer must parse the `FZFRAME` header and payload, then apply the target device's crop, scale, rotation, connector/mode selection, and DRM-lease scanout. It should not assume the exported frame has the native 3DS bottom-screen dimensions: it is the Azahar secondary window's rendered size.

The current MVP intentionally blocks on GPU completion and writes a file every exported frame. Replace it with persistent/ringed staging buffers and a more efficient handoff (for example shared memory or dma-buf) only after end-to-end visual correctness is established.

Bottom-touch forwarding into Azahar is not implemented. It must be added independently of gamescope input handling and mapped for the target device.

### Portability boundary

The source implementation contains no hardcoded connector, DRM lease socket, touch device, display mode, rotation, or output path. `--flipds-*` is a historical name, not device detection. It can export Vulkan secondary frames on another compatible Linux device by selecting an output path at launch.

The existing Flip DS deployment documentation is device-specific: its gamescope lease configuration assumes `eDP-1`/`DP-1`, the lower-panel rotation and mode, a particular lease socket, and Goodix touch hardware. Other devices require a matching lease-consumer configuration; they are not plug-and-play from this repository alone.
