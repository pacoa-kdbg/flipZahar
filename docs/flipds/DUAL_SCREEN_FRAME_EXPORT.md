# flipZahar / Flip DS 1S dual-screen frame export plan

This fork exists to turn Azahar's existing Separate Windows architecture into a native AYANEO Flip DS 1S gamescope/DRM-lease path.

## Device target

```text
Device: AYANEO Flip DS 1S on CachyOS / Steam Game Mode
Top/main panel: eDP-1, 1920x1080 OLED, owned by gamescope/Steam
Lower panel: DP-1, 1080x1620 LCD, leased by mmogr/gamescope over /tmp/gamescope-lease.sock
Lower touch: Goodix /dev/input/event7, ignored by gamescope while a lease client is connected
```

## Current external prototype

The current no-Azahar-patch prototype works only when Azahar uses the OpenGL backend:

```text
Azahar OpenGL secondary/bottom X window
  -> ImageMagick/X11 capture loop
  -> /tmp/azahar-bottom.ppm
  -> flipds-azahar-lease.service
  -> flipds-lease-dashboard --socket /tmp/gamescope-lease.sock --frame /tmp/azahar-bottom.ppm --rotate 90cw
  -> DP-1 lower LCD
```

This proves physical routing and gamescope lease ownership, but it is not the target architecture.

Why it is not enough:

- X11 window capture cannot read Azahar Vulkan/Gamescope WSI surfaces; captures were black.
- CPU screenshot/capture polling adds latency and overhead.
- The bridge heuristically selects the bottom window by image statistics.
- Bottom touch is not injected into Azahar yet.

## Findings from source inspection

Azahar already has the key architectural primitives:

- `src/core/core.h`: `System::Load(..., Frontend::EmuWindow* secondary_window = {})`
- `src/citra_qt/citra_qt.cpp`: creates both `render_window` and `secondary_window`
- `src/core/frontend/framebuffer_layout.cpp`: `SeparateWindowsLayout()` maps one screen per window
- `src/core/frontend/emu_window.cpp`: touch hit testing is already aware of Separate Windows
- `src/video_core/renderer_opengl/renderer_opengl.cpp`: renders secondary layout to `secondary_window->mailbox`
- `src/video_core/renderer_vulkan/renderer_vulkan.cpp`: renders secondary layout via `secondary_present_window_ptr`
- `src/video_core/renderer_vulkan/vk_present_window.cpp`: has frame images with `eTransferSrc`, which is the likely place to add readback/export plumbing

Important Vulkan hook:

```cpp
void RendererVulkan::SwapBuffers() {
    ...
    RenderToWindow(main_present_window, layout, false);
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        ...
        isSecondaryWindow = true;
        RenderToWindow(*secondary_present_window_ptr, secondary_layout, false);
        secondary_window->PollEvents();
    }
}
```

Important render-to-window hook:

```cpp
void RendererVulkan::RenderToWindow(PresentWindow& window,
                                    const Layout::FramebufferLayout& layout,
                                    bool flipped) {
    Frame* frame = window.GetRenderFrame();
    ...
    DrawScreens(frame, layout, flipped);
    scheduler.Flush(frame->render_ready);
    window.Present(frame);
}
```

A first Vulkan frame-export implementation should copy the **secondary** `Frame::image` after `DrawScreens()` and before/around `Present()`.

## First fork changes already added

This fork now has launcher-facing CLI plumbing so Steam shortcuts no longer need to patch Qt INI files:

```text
--separate-windows
--flipds-dual-screen
--graphics-api software|opengl|vulkan
--flipds-bottom-frame-export <path>
```

Current behavior:

- `--flipds-dual-screen` / `--separate-windows` forces Separate Windows, disables swap/upright, disables single-window mode, and keeps the top window windowed.
- `--graphics-api` forces `Settings::values.graphics_api` to Software/OpenGL/Vulkan for this launch.
- `--flipds-bottom-frame-export` currently sets `FLIPZAHAR_BOTTOM_FRAME_EXPORT` as a stable handoff path for the renderer-export implementation.

Example future Steam launcher shape:

```bash
flipzahar \
  --flipds-dual-screen \
  --graphics-api vulkan \
  --flipds-bottom-frame-export /tmp/flipzahar-bottom-frame.rgba \
  /path/to/game.3ds
```

## Target architecture

```text
Azahar Vulkan/OpenGL secondary render output
  -> explicit bottom-frame export from renderer
  -> shared memory / socket / dmabuf handoff
  -> FlipDS DRM lease renderer
  -> DP-1 lower LCD
```

### Phase 1: raw frame export

Goal: correctness before performance.

- Export secondary/bottom frames as a simple fixed header + BGRA/RGBA payload.
- Use one file or shm path named by `--flipds-bottom-frame-export`.
- Write atomically enough that the lease renderer never reads a partially updated header.
- Include width, height, stride, format, monotonically increasing frame counter, and timestamp.

Suggested file format for a prototype:

```c
struct FlipZaharFrameHeader {
    char magic[8];       // "FZFRAME\0"
    uint32_t version;    // 1
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;     // e.g. BGRA8 = 1
    uint64_t frame_index;
    uint64_t monotonic_ns;
};
// followed by stride * height bytes
```

### Phase 2: Vulkan readback implementation

Start from the existing screenshot path in `renderer_vulkan.cpp`:

- `RenderScreenshotWithStagingCopy()` already creates a host-visible transfer-destination buffer.
- It records `copyImageToBuffer()` from a render image.
- It waits for scheduler completion before reading host memory.

For live export, avoid blocking every frame forever, but the first implementation can block at low FPS just to prove routing.

Recommended incremental path:

1. Add a helper that copies `Frame::image` to a host-visible staging buffer for the secondary `RenderToWindow()` path only.
2. Write the raw file/shm frame.
3. Validate on the Flip DS with a safe homebrew app.
4. Then optimize with persistent staging buffers or dmabuf.

### Phase 3: lease renderer consumer

Update the companion/lease renderer to consume the raw frame format instead of PPM polling.

Keep the already-proven lease path:

```text
/home/deck/.local/bin/flipds-lease-dashboard \
  --socket /tmp/gamescope-lease.sock \
  --frame /tmp/flipzahar-bottom-frame.rgba \
  --interval-ms 16 \
  --rotate 90cw
```

The current `--rotate 90cw` correction came from physical lower-screen validation. Do not infer orientation from copied screenshots alone.

### Phase 4: bottom touch IPC

Use the companion Goodix input reader as the source of truth:

```text
/dev/input/event7 raw 0..1079 x 0..1619
```

Future Azahar IPC shape:

```json
{"type":"touch","pressed":true,"x":142,"y":97}
```

Inject into Azahar's secondary-window touch path rather than sending lower touches to Steam/gamescope. Keep `--ignore-touch-device Goodix` in gamescope while DP-1 is leased.

## Testing plan

### Local/static

- `git diff --check`
- build or at least compile the touched Qt frontend if dependencies are available
- run `flipzahar --help` once a binary exists and verify the new flags are present

### On Flip DS 1S

1. Keep current AppImage as rollback.
2. Deploy forked binary as a separate executable, e.g. `/home/deck/Applications/flipZahar/flipzahar`, not over the existing Azahar AppImage.
3. Use the existing safe homebrew test first: `/home/deck/Applications/azahar-dual-test/3ds-app-test.3dsx`.
4. Validate top focus:

```bash
DISPLAY=:0 xprop -root GAMESCOPE_FOCUSED_APP GAMESCOPE_FOCUSED_APP_GFX
```

Expected Steam app id for the test shortcut:

```text
2640455173
```

5. Validate bottom frame updates:

```bash
stat -c '%n %s %y' /tmp/flipzahar-bottom-frame.rgba
sleep 1
stat -c '%n %s %y' /tmp/flipzahar-bottom-frame.rgba
```

6. Only after homebrew works, test a real game in a save-safe way. Do not start/create new saves unless Paco explicitly asks.

## Agent handoff notes

If another coding agent picks this up, start with `renderer_vulkan.cpp::RenderToWindow()` and `vk_present_window.cpp` rather than Qt window placement. Normal compositor placement has already been ruled out on this Flip DS gamescope stack.
