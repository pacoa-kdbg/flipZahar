# flipZahar build, deploy, and validation notes

This document records the current practical build/deploy path for Paco's `pacoa-kdbg/flipZahar` fork on the AYANEO Flip DS 1S.

## Repository layout

Local development checkout on the Azure/Hermes VM:

```text
/home/pacoavelar/.openclaw/workspace/projects/azahar
```

GitHub fork:

```text
https://github.com/pacoa-kdbg/flipZahar
```

Upstream:

```text
https://github.com/azahar-emu/azahar
```

Flip DS working copy used for on-device build probes:

```text
/home/deck/src/flipZahar-work
```

## Why build on the Flip DS or x86_64 CI

The current Hermes/Azure machine is ARM64/aarch64 and lacks Qt6 development packages, so it is not a good place to produce a deployable x86_64 AppImage for the Flip DS.

Observed local configure blocker:

```text
Could not find Qt6Config.cmake / qt6-config.cmake
```

The Flip DS is x86_64 and has the right Qt6/build stack installed.

## Flip DS environment

Observed host:

```text
deck@flipds1scachy
CachyOS / x86_64
```

Observed build tools/packages:

```text
cmake 4.4.0
ninja 1.13.2
gcc 16.1
clang 22.1
git
ctest
lld
qt6-base
base-devel
```

Missing or not guaranteed:

```text
ccache
Docker/Podman
appimagetool
linuxdeploy
patchelf
```

Do not rely on Azahar's CI script directly on-device unless `ccache` and its expected linker paths are installed. Prefer the manual CMake/Ninja route below.

## Clone / sync

Fresh clone on Flip DS:

```bash
mkdir -p ~/src
cd ~/src
git clone --recursive https://github.com/pacoa-kdbg/flipZahar.git flipZahar
cd ~/src/flipZahar
```

If submodules are missing:

```bash
git submodule update --init --recursive
```

For agent-driven sync from the Hermes workspace, `rsync` works but be careful with `--delete`:

```bash
rsync -a --delete \
  --exclude .git \
  --exclude build-flipzahar-verify \
  /home/pacoavelar/.openclaw/workspace/projects/azahar/ \
  flipds1s:/home/deck/src/flipZahar-work/
```

Use `ssh flipds1s bash -s <<'REMOTE'` for multi-line commands because the `deck` account uses `fish` as its login shell and complex Bash one-liners can fail under fish.

## Manual on-device configure

A lightweight verification configure used successfully on the Flip DS:

```bash
cd /home/deck/src/flipZahar-work
cmake -S . -B build-flipzahar-verify -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_QT=ON \
  -DENABLE_SDL2=OFF \
  -DENABLE_TESTS=OFF \
  -DENABLE_ROOM=OFF \
  -DENABLE_DISCORD_RPC=OFF \
  -DENABLE_QT_TRANSLATION=OFF \
  -DENABLE_QT_UPDATE_CHECKER=OFF
```

Observed result: configure succeeded and generated build files.

## Manual on-device build

Build the Qt app target:

```bash
cd /home/deck/src/flipZahar-work
cmake --build build-flipzahar-verify --target azahar -j4
```

If the handheld is under load or SSH becomes unstable, retry/resume with lower parallelism:

```bash
cmake --build build-flipzahar-verify --target azahar -j2
```

Current status from the first probe:

- Build got through the touched source file `src/citra_qt/citra_qt.cpp`.
- Build reached the Qt target/link area (`src/citra_qt/libcitra_qt.a`) before foreground SSH timeout.
- A later SSH attempt timed out, so final binary/link completion was not verified. Do not claim an on-device deploy until this is rechecked.

To check whether the binary exists after reconnecting:

```bash
ssh -o BatchMode=yes -o ConnectTimeout=20 flipds1s bash -s <<'REMOTE'
set -euo pipefail
cd /home/deck/src/flipZahar-work
find build-flipzahar-verify -type f -perm -111 \( -name azahar -o -name '*azahar*' \) -print
if [ -x build-flipzahar-verify/bin/azahar ]; then
  build-flipzahar-verify/bin/azahar --help | grep -E 'flipds|separate-windows|graphics-api'
fi
REMOTE
```

Expected help output should include:

```text
--separate-windows
--flipds-dual-screen
--graphics-api [api]
--flipds-bottom-frame-export [path]
```

## Full deployable AppImage path

For a packaged AppImage, use a fuller build and the bundle target on the Flip DS or x86_64 CI:

```bash
cd ~/src/flipZahar
git submodule update --init --recursive

cmake -S . -B build-flip-wayland -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_LINKER=/usr/bin/ld.lld \
  -DENABLE_ROOM_STANDALONE=OFF \
  -DENABLE_DISCORD_RPC=ON

cmake --build build-flip-wayland --parallel "$(nproc)"

strip -s build-flip-wayland/bin/Release/*

EXTRA_QT_PLUGINS=waylandcompositor \
EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so' \
cmake --build build-flip-wayland --target bundle --parallel "$(nproc)"
```

Expected AppImage location:

```text
build-flip-wayland/bundle/azahar.AppImage
```

## Safe deploy rule

Do **not** overwrite the current known Azahar AppImage:

```text
/home/deck/Applications/azahar-wayland.AppImage
```

Deploy fork builds under a new name:

```bash
mkdir -p /home/deck/Applications/flipZahar
cp -n build-flip-wayland/bundle/azahar.AppImage \
  /home/deck/Applications/flipZahar/flipZahar-wayland-test.AppImage
chmod +x /home/deck/Applications/flipZahar/flipZahar-wayland-test.AppImage
```

## Smoke tests

Basic AppImage runtime check:

```bash
/home/deck/Applications/flipZahar/flipZahar-wayland-test.AppImage --appimage-help
```

Known-good gamescope/Xwayland env shape:

```bash
env \
  XDG_RUNTIME_DIR=/run/user/1000 \
  DISPLAY=:1 \
  WAYLAND_DISPLAY=gamescope-0 \
  QT_QPA_PLATFORM=xcb \
  LANG=en_US.UTF-8 \
  /home/deck/Applications/flipZahar/flipZahar-wayland-test.AppImage \
  --flipds-dual-screen \
  --graphics-api vulkan \
  --flipds-bottom-frame-export /tmp/flipzahar-bottom-frame.rgba \
  /home/deck/Applications/azahar-dual-test/3ds-app-test.3dsx
```

Use the safe homebrew test first:

```text
/home/deck/Applications/azahar-dual-test/3ds-app-test.3dsx
```

Do not use a real game/save until the homebrew path proves focus, frame export, DP-1 scanout, and rollback.

## Runtime validation checklist

Top focus:

```bash
DISPLAY=:0 xprop -root GAMESCOPE_FOCUSED_APP GAMESCOPE_FOCUSED_APP_GFX
```

For the existing test shortcut, expected app id:

```text
2640455173
```

Bottom frame export mtime should change:

```bash
stat -c '%n %s %y' /tmp/flipzahar-bottom-frame.rgba
sleep 1
stat -c '%n %s %y' /tmp/flipzahar-bottom-frame.rgba
```

Bridge/lease logs:

```bash
journalctl --user -u azahar-bottom-bridge.service -u flipds-azahar-lease.service \
  --since '2 minutes ago' --no-pager -l
```

## Existing tests

Azahar has a test target when `ENABLE_TESTS=ON`:

```bash
ctest --test-dir build-flip-wayland -VV -C Release
# or direct, depending build tree layout:
./build-flip-wayland/bin/Release/tests
```

Existing tests are mostly common/core/audio/video shader tests; there are not robust backend-renderer runtime tests for this feature yet.

Good low-risk tests to add before more renderer work:

- `SeparateWindowsLayout(..., is_secondary=true)` is bottom-only.
- `swap_screen` inverts expected top/bottom ownership.
- raw frame export header serialization/deserialization.

## Dependency notes

Debian/Ubuntu-style dependencies if building elsewhere:

```bash
sudo apt install build-essential clang clang-format cmake ninja-build git ccache lld \
  jackd libasound-dev libgl-dev libpipewire-0.3-dev libsndio-dev libssl-dev \
  libsdl2-dev libx11-dev libxext-dev qt6-base-dev qt6-base-private-dev \
  qt6-l10n-tools qt6-multimedia-dev qt6-tools-dev qt6-tools-dev-tools xorg-dev
```

Arch/CachyOS-style dependencies:

```bash
sudo pacman -S --needed base-devel clang cmake ninja git lld ccache \
  qt6-base qt6-multimedia qt6-multimedia-ffmpeg qt6-tools \
  sdl2-compat libxkbcommon-x11 vulkan-headers vulkan-icd-loader libusb openal
```
