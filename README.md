# Camera Colony

An independent third-person camera mod for Skyrim, with a Rust camera core and a
C++ bridge built on CommonLibSSE-NG. Smooth movement, adjustable offsets and
shoulder switching, while retaining Skyrim's native collision handling.

## Versions and source

- [0.1.2 alpha](https://github.com/MotherSphere/Colony-Camera/tree/v0.1.2) is the
  source corresponding to the Nexus release. All 25 files were compared with
  the source archive packaged with that build.
- The default branch contains **0.1.3 diagnostic**, which adds sampled CPU
  timing to investigate a reported slowdown without changing camera behavior.
- Original project code is **GPL-3.0-or-later**. See [LICENSE](LICENSE) and
  [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

The plugin and configuration filenames remain `ColonyCamera.dll` and
`ColonyCamera.ini`. This is an independent project, not a SmoothCam release or
an implementation of its complete feature set.

## 0.1.2 distribution update

[Download the corrected package and corresponding source](https://github.com/MotherSphere/Colony-Camera/releases/tag/v0.1.2).
The install ZIP retains the original DLL and INI and includes a consolidated
`LICENSES.txt` plus a short `README.txt` linking to the exact corresponding source.
It contains no source directories, checksum manifest or revision file.
The full project/dependency source ZIP is a separate release asset.
Upstream notices are also retained in `licenses/` in this repository.
This is a packaging correction, not a camera behavior or version change.

## Features

- Frame-rate-independent position smoothing with a maximum lag distance.
- Separate exploration, weapon-drawn combat and aiming profiles.
- Additional horizontal, depth and height offsets in camera space.
- Native shoulder switching, with immediate restoration of Skyrim's settings.
- Native collision handling after smoothing, retaining its correction.
- Resets after loading, cell changes, teleports, entering third person and long
  update interruptions.
- Atomic configuration validation and on-demand INI reloading.
- First-person, mounted, menu and dialogue camera states excluded from processing.
- Automatic disabling when SmoothCam is loaded alongside this plugin.

The aiming profile keeps the native position by default to preserve crosshair
alignment. Changing it may affect aiming accuracy. This alpha does not include
an MCM menu, a custom crosshair, projectile prediction or SmoothCam preset imports.
Full compatibility with TDM, Improved Camera, dialogue systems or other camera
managers is not guaranteed.

## Requirements and installation

The only supported target is **Skyrim Steam 1.7.104.0**, **SKSE 2.3.1** and the
matching Address Library. The DLL rejects other runtimes. No ESP or Papyrus
scripts are needed. The Windows x64 build has been observed working under Proton;
this does not establish compatibility with every setup.

1. Close Skyrim, back up your configuration and disable SmoothCam and other full
   third-person camera replacements. Improved Camera can remain enabled; its
   collision hook is preserved.
2. Install the chosen version with your mod manager. It must provide
   `SKSE/Plugins/ColonyCamera.dll` and `SKSE/Plugins/ColonyCamera.ini`.
3. Launch through SKSE and load a test save or start a new game.
4. If the plugin refuses to load or has no visible effect, inspect
   `Documents/My Games/Skyrim Special Edition/SKSE/ColonyCamera.log` inside the
   appropriate Windows or Proton prefix.

Default keyboard shortcuts, with menus closed and the camera in third person:

| Shortcut | Action |
| --- | --- |
| Ctrl+F8 | Toggle the effect |
| Ctrl+F9 | Switch shoulders |
| Ctrl+F10 | Reload the INI |

A shortcut used in first person is processed on the next transition to third
person. Keys use SKSE scan codes; this alpha has no controller shortcuts. Toggle
and shoulder changes are not written to the save or INI.

In the INI, `x/y/z` are offsets added to the native camera, in game units, limited
to +/-300. `half_life` is the time in seconds to halve the remaining error
(0 means immediate). `max_lag` limits the distance from the requested position
(0 means immediate). A shorter combat half-life gives a faster response. Default
offsets are zero: smoothing is the main out-of-the-box effect.

To uninstall, close the game and disable the mod. No vanilla files, gameplay
plugins or save data are modified.

## Building

Requirements: Rust/Cargo, Python 3, Git, CMake >=3.24 and C++23. Native dependency
revisions are pinned in [dependencies.json](dependencies.json); their separate
source checkouts live in `deps/`.

```sh
python3 scripts/fetch-dependencies.py
cargo test --locked
cargo clippy --all-targets -- -D warnings
rustup target add x86_64-pc-windows-msvc
```

On Linux, install clang-cl, lld-link, llvm-lib, llvm-rc, llvm-mt and Ninja, plus an
xwin sysroot containing `crt/` and `sdk/` (default: `~/.local/share/xwin`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux-xwin.cmake
cmake --build build -j 8
```

On Windows, use a Visual Studio developer terminal with C++ tools and Cargo on PATH.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

To rebuild from a full source package, extract `colony-camera.tar.gz`, then
extract `dependencies.tar.gz` inside the resulting `Colony-Camera` directory.
These `deps/` folders contain source without Git metadata: skip
`fetch-dependencies.py` and run CMake directly.

CMake builds the Rust core automatically. Both languages use the static CRT.
The Linux build path has been exercised; the Visual Studio path remains untested.
The `camera_abi` and `camera_load` executables check the cross-language interface,
binary guards and Windows DLL loading without starting Skyrim.

```sh
# Linux/Wine: use a test prefix separate from the game prefix.
WINEPREFIX="$PWD/build/wine" wine build/camera_abi.exe
WINEPREFIX="$PWD/build/wine" wine build/camera_load.exe "$(WINEPREFIX="$PWD/build/wine" winepath -w "$PWD/build/ColonyCamera.dll")"
python3 scripts/verify-runtime.py /path/to/SkyrimSE.exe /path/to/versionlib-1-7-104-0.bin
cmake --install build --prefix dist/staging
```

## Provenance and license

Original project code is GPL-3.0-or-later. No SmoothCam source files, assets,
scripts, menus or presets are included. Dependencies include CommonLibSSE-NG
(alandtse and contributors), spdlog, DirectXMath and DirectXTK. Their revisions,
licenses and credits are recorded in `dependencies.json` and
`THIRD-PARTY-NOTICES.md`.

Public [SmoothCam source](https://github.com/mwilsnd/SkyrimSE-SmoothCam/tree/66f3960ec4de2b28af5e863c794a3924e6a2dfdd)
was studied to understand deferred camera integration and publication to NiCamera.
The CommonLib bridge is implemented in this project. Inspecting engine entry
points does not establish in-game correctness or comprehensive compatibility.
The checks described above do not replace in-game testing.

When distributing the binary, provide this source link and the corresponding
version, and retain the supplied license and third-party notices.

## Camera fixes

### 0.1.1

The initial alpha stopped processing when another plugin intercepted camera
functions. Hooks now install after loading or starting a game, preserving existing
callbacks. The result is published to the camera state, scene root and render
node, then the engine recalculates the projection matrix.

Logs distinguish hook installation, callback execution, the first applied frame
and the first nonzero displacement. These messages are not emitted every frame.
Animations without player control and killmoves retain native behavior.

### 0.1.2

The 0.1.1 callback ran after TDM/SkyParkour but rejected a missing NiCamera or a
parented root under the same diagnostic. A parent is now allowed: its inverse
transform produces the root's local position while preserving world positions
and the render child's own offset. Invalid transforms leave positions unchanged.
Logs distinguish a missing root, child types, invalid transforms and successful
application. Improved Camera detection recognizes `ImprovedCamera.dll`.
The user subsequently confirmed a visible effect; details are in the verification
record. Restarting Skyrim is required after replacing the DLL.

## Performance diagnostics in 0.1.3

A user reported 200 FPS with the effect disabled versus 170 FPS while running
and turning with it enabled. That report alone does not identify the source of
the cost. This diagnostic version keeps camera behavior unchanged and measures
one update in sixteen. It reports after 64 samples and when toggling Ctrl+F8;
there is no per-frame performance logging.

`PERF` separates enabled and disabled samples. `mean_us` reports microseconds:
`own` excludes `previous_chain` (engine and other plugins); `collision`, `rust`
and `scene` describe portions of the plugin's time. Averages cover every sample
in the block, including samples with no effect; `applied` counts published
positions. `peak_own_us` is a sampled maximum, not a guaranteed worst frame or a
percentile.

For comparison, run and turn for twenty seconds, toggle Ctrl+F8, repeat the same
route for twenty seconds, then re-enable. Avoid menus, loading and configuration
changes during the comparison, and include `ColonyCamera.log` with your report.

These wall-clock timings include possible OS interruptions. They measure neither
GPU cost nor complete frame time. Command handling and reports outside the probe
are excluded. The instrumentation has an uncalibrated cost of its own: it helps
locate overhead, but does not certify an FPS loss.
