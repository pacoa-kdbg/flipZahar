# flipZahar agent handoff

This repository is Paco's AYANEO Flip DS 1S fork of Azahar.

## Mission

Make 3DS dual-screen gameplay work naturally on the Flip DS 1S in Steam/Game Mode:

```text
Top 3DS screen    -> eDP-1 OLED via Steam/gamescope
Bottom 3DS screen -> DP-1 lower LCD via gamescope DRM lease
Bottom touch      -> Azahar bottom-screen touch input
```

## Do not repeat already-ruled-out paths

1. Do **not** try to drag/place normal Azahar Qt secondary windows onto DP-1 in the current gamescope session. Tested result: both windows stay in the top gamescope output.
2. Do **not** assume removing `--lease-connector DP-1` makes DP-1 a normal compositor output. Tested result: gamescope still selected only eDP-1 and left DP-1 disabled.
3. Do **not** rely on X11 capture for Vulkan. Tested result: Vulkan/Gamescope WSI XIDs captured black.
4. Do **not** change the normal companion dashboard rotation when fixing Azahar bridge rotation. Dashboard uses `270cw`; current Azahar bridge uses `90cw`.

## Current useful prototype outside this repo

The no-patch prototype on the Flip DS currently uses OpenGL and X capture:

```text
/home/deck/Applications/azahar-dual-test/launch-dual-screen-test-opengl.sh
/home/deck/Applications/azahar-dual-test/azahar-bottom-capture-loop.py
/home/deck/.config/systemd/user/azahar-bottom-bridge.service
/home/deck/.config/systemd/user/flipds-azahar-lease.service
```

It is useful as a reference for service orchestration and physical orientation, not as the final renderer path.

## Current fork changes

See:

- `docs/flipds/DUAL_SCREEN_FRAME_EXPORT.md`
- `docs/flipds/BUILD_AND_DEPLOY.md`

Initial CLI plumbing added:

```text
--separate-windows
--flipds-dual-screen
--graphics-api software|opengl|vulkan
--flipds-bottom-frame-export <path>
```

These are launch/profile affordances. The actual renderer frame export is the next code task.

## Highest-value next coding task

Implement a first Vulkan secondary-frame raw export:

1. In `src/video_core/renderer_vulkan/renderer_vulkan.cpp`, identify when `RenderToWindow()` is rendering the secondary window (`isSecondaryWindow == true`).
2. After `DrawScreens(frame, layout, flipped)`, copy `frame->image` into a host-visible staging buffer.
3. Write a simple raw frame file/shm path named by `FLIPZAHAR_BOTTOM_FRAME_EXPORT`.
4. Make it correct before fast. Blocking copy is acceptable for the first on-device proof.
5. Then optimize toward persistent buffers/dmabuf.

Reference code to borrow from:

```text
RendererVulkan::RenderScreenshotWithStagingCopy()
```

It already demonstrates:

- host-visible staging buffer allocation,
- image-to-buffer copy,
- scheduler synchronization,
- host memory access.

## Testing target

Use safe homebrew first:

```text
/home/deck/Applications/azahar-dual-test/3ds-app-test.3dsx
```

Do not use a real game/save until the homebrew path proves top focus + bottom frame updates.

## GitHub / repo

Fork name:

```text
pacoa-kdbg/flipZahar
```

Upstream:

```text
azahar-emu/azahar
```

Keep upstream-friendly changes separate from Flip-specific experiments when possible:

- Generic flags like `--separate-windows` and `--graphics-api` may be upstreamable.
- `--flipds-*`, gamescope DRM lease handoff, and Goodix touch IPC are probably fork-specific at first.
