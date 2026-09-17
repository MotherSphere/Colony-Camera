# Camera Colony

An independent Skyrim camera plugin with a deterministic Rust core and a C++
CommonLibSSE-NG bridge. **0.2.1 is a development candidate** with configurable
third-person transitions, native settings menus and an opt-in first-person body
experiment. It adds adjustable horizontal body alignment to investigate poor
first-person framing. It is not a complete SmoothCam or Improved Camera replacement.

The filenames remain `ColonyCamera.dll` and `ColonyCamera.ini`.
[0.1.2 and its corresponding sources](https://github.com/MotherSphere/Colony-Camera/releases/tag/v0.1.2)
remain the published historical release; its tag and binary are unchanged.

## Candidate behavior and limits

- Time-based third-person following with bounded lag, native collision, shoulder
  switching and parent-aware camera publication. Position, camera-space offsets,
  additional depth and world-FOV adjustment have independent half-lives.
- Exploration, combat and aiming profiles, plus optional locomotion overrides.
  Precedence is **aim > swimming > sneaking > sprinting > airborne > combat >
  exploration**. Locomotion overrides are off in the supplied configuration.
- One coordinator selects native, third-person or first-person ownership. Menus,
  unavailable controls, furniture, mounts, transformations, ragdoll, death and
  killmoves return to native behavior. First-person swimming and airborne states
  also use native behavior. These fallbacks are not immersive support for those
  actions; custom races and skeletons may also fall back conservatively.
- Native modal settings pages with field/profile defaults, binding edits, save,
  reload and Camera Colony presets. No SkyUI, Papyrus scripts, ESP or external
  interface assets are required.

The **first-person body experiment is off by default**. It attempts to display a
conventional humanoid third-person body while retaining Skyrim's native camera
and first-person arms. Head/third-person arm masking and equipment visibility
are temporary, with restoration when the plugin still owns the changed values.
Missing or replaced skeleton nodes cause a native fallback.

User screenshots of 0.2.0 confirm body visibility, but the torso and shoulders
occupy too much of the view. The 0.2.1 alignment attempts to place the body
horizontally relative to the native eye and head, with adjustable backset and
lateral offset. It does not rescale the body, shift it vertically or alter the
native camera/FOV. **This framing correction is not yet visually validated.**

Alignment, collision, projectile origins, interiors, head/hair/helmet clipping,
weapons, shields, torches, spell effects and shadows still require testing.
Third-person arms, head-motion controls, independent hands FOV, near-plane adjustment and immersive
furniture/mount/transformation/death/killmove handling are not implemented.
There is no dynamic crosshair, projectile-origin reconciliation, ballistic
prediction, trajectory display or third-party preset import mapping. Aiming
defaults preserve the native camera; custom aiming displacement can reduce accuracy.

## Requirements and safe installation

The only target is **Steam Skyrim 1.7.104.0**, **SKSE 2.3.1** and the matching
Address Library. Other runtimes, GOG and VR are unsupported. Exact runtime and
entry-point checks reject unexpected code before the affected hooks are installed.

1. Close Skyrim and back up your installed DLL and INI. Install the candidate as
   a separate mod providing `SKSE/Plugins/ColonyCamera.dll` and `ColonyCamera.ini`.
   Keep your existing INI; merge new fields or use the settings menu.
2. Disable SmoothCam before testing: detecting `SmoothCam.dll` prevents Camera
   Colony's camera hooks from installing. A detected `ImprovedCamera.dll` or
   `ImprovedCameraSE.dll` disables only this plugin's first-person body experiment.
   Other full camera providers are not comprehensively detected.
3. Launch through SKSE when ready to test. Start with first-person body visibility
   off. Enable it separately after checking ordinary third-person operation.
4. Inspect `ColonyCamera.log` in the active SKSE log directory, normally under
   `Documents/My Games/Skyrim Special Edition/SKSE`. Documents may be redirected;
   Proton uses the game's actual prefix.

TDM, SkyParkour, animation/skeleton changes, ENB, Community Shaders and body mods
require separate compatibility tests. Hook chaining does not establish visual or
behavioral compatibility. No mod-manager profile is changed automatically.
To roll back, close Skyrim, disable the candidate and restore the backed-up DLL
and INI. The plugin does not write camera settings into saves.

## Settings and controls

With gameplay active and other menus closed:

| Shortcut | Action |
| --- | --- |
| Ctrl+F7 | Open settings |
| Ctrl+F8 | Toggle the effect |
| Ctrl+F9 | Switch shoulder |
| Ctrl+F10 | Reload the saved INI |

Settings use Skyrim's modal message-box menu for focus and normal keyboard or
controller navigation. The pages are **General, Third Person, First Person,
Aiming, Presets, Compatibility and Diagnostics**. There is no controller opening
chord or press-to-bind capture. General > Controls edits numeric SKSE keyboard
scan codes; all shortcuts require Ctrl. The original F8/F9/F10 identities remain.

Numeric fields use Increase/Decrease and ten-step buttons. Changes apply in
memory immediately. **General > Save settings** persists them by flushing a
temporary file, then replacing the INI while keeping its previous version as
`ColonyCamera.ini.bak`. Saving writes the canonical format; old comments remain
in the backup. Reload rejects malformed files and retains active settings.
Closing the menu alone does not save changes. Shoulder choice is session-only.

INI format 3 accepts formats 1 and 2, preserving bindings and the existing
first-person enabled state. Missing alignment fields use the new defaults;
the body experiment remains off in a fresh configuration. Missing locomotion
sections remain inactive. Missing `offset_half_life` inherits an explicitly supplied
legacy `half_life`. Existing key remaps are retained; if an older binding already
uses F7, an unused menu key is chosen. Unknown/duplicate fields, conflicting keys,
non-finite numbers and out-of-range values reject the entire file.

First Person provides the body toggle, an alignment toggle and numeric backset/
lateral controls. In `[first_person]`, `alignment_enabled=true` requests horizontal
alignment, `body_backset=12` moves the body backward (0-40), and `body_side=0`
moves it laterally (-20 to 20; positive is the player's right). Units are relative
to skeleton scale 1. Unsafe positions or excessive translations retain native
placement. Disable alignment to compare the previous framing while keeping the
body experiment enabled. Save persists these settings; the native camera and
first-person arms remain unchanged.

| Profile setting | Meaning and accepted range |
| --- | --- |
| `x`, `y`, `z` | Added camera-space lateral/depth/height offset, -300 to 300 game units |
| `half_life` | Position error half-life, 0 to 1 second; 0 follows immediately |
| `max_lag` | Position lag cap, 0 to 300 game units; 0 removes following lag |
| `offset_half_life` | Offset/profile/shoulder transition half-life, 0 to 1 second |
| `zoom` | Additional camera-local depth, -300 to 300; added to `y`, preserving native zoom input |
| `zoom_half_life` | Additional-depth transition half-life, 0 to 1 second |
| `fov_offset` | World-FOV delta, -60 to 60 degrees; 0 requests native FOV |
| `fov_half_life` | World-FOV delta transition half-life, 0 to 1 second |
| `active` | Enable an optional sprint/sneak/swim/airborne override |

Half-lives use exponential interpolation; there are no other easing modes.
Nonzero FOV adjustments target 30-150 degrees, with smooth return to native FOV.
Collision is applied after the requested third-person motion. Defaults use zero
offsets, depth and FOV adjustments; the aiming profile also has zero lag.

Presets provides Balanced, Responsive and Native position profiles. Export writes
`Data/SKSE/Plugins/ColonyCamera/Presets/User.ccpreset`; rename/copy that file to keep
named presets. Import lists this folder and preserves your current bindings.
The `.ccpreset` format is Camera Colony's versioned INI format. Importing applies
settings in memory; General > Save persists them to the main INI.

## Building and checks

Use Git, Python 3.11+, Rust/Cargo with the Windows MSVC target, CMake 3.24+ and a
C++23 toolchain. Dependencies are pinned in [dependencies.json](dependencies.json).

```powershell
python scripts/fetch-dependencies.py
rustup target add x86_64-pc-windows-msvc
cargo fmt --check
cargo test --locked
cargo clippy --all-targets -- -D warnings
python -m unittest discover -s tests -p "test_*.py"
```

On Windows, open an **x64 Visual Studio developer terminal** with the Windows SDK,
Ninja and Cargo on PATH. CMake sets the required MSVC preprocessor/conformance and
large-object options. The native baseline was built using MSVC 19.44 and Rust
1.98.1 on Windows 11.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

For the Visual Studio generator, configure a separate directory with
`cmake -S . -B build-vs -A x64`, then use `--config Release` for building and
`-C Release` for CTest. CMake builds the Rust core automatically; both languages
use the static CRT. The native test executables check ABI/layout and math,
ownership/restoration and DLL loading/rejection without launching Skyrim.

The existing Linux cross-build needs clang-cl, lld-link, llvm-lib, llvm-rc,
llvm-mt, Ninja and an xwin sysroot containing `crt/` and `sdk/`:

```sh
cmake -S . -B build-cross -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux-xwin.cmake
cmake --build build-cross --parallel 2
```

That toolchain defaults to `~/.local/share/xwin`; override `XWIN_ROOT` as needed.
Its Windows test executables require Windows or a separate Wine test prefix.
The 0.2.1 cross-build is not yet verified.

Validate entry bytes, Address Library mappings and camera RTTI against a
legitimate local game installation:

```powershell
python scripts/verify-runtime.py "<game>/SkyrimSE.exe" "<game>/Data/SKSE/Plugins/versionlib-1-7-104-0.bin"
```

The 0.2.1 Windows x64 DLL compiled and passed export, load and null-SKSE rejection
checks; configuration persistence and timing checks passed. Rust formatting and
Clippy passed, along with 23 targeted tests: 8 alignment, 7 configuration, 3 FFI
and 5 coordinator tests. Runtime verification passed 10 entry checks, two camera
RTTI checks and five negative cases.

The full suite is **not green**: Windows Application Control blocked the existing
15-test Rust camera target and the final native ABI, hook and body-position test
executables. The unchanged ownership executable remains blocked and was not
rerun. **0.2.1 visual alignment validation remains pending.** The 0.2.0 screenshots
confirm body visibility and poor framing, not aiming, collision or compatibility
correctness. Compilation cannot verify rendering.

Before using a candidate broadly, check new/load game, repeated POV switching,
walk/run/sprint, slopes/stairs/tight walls, aim/spells, menu/dialogue transitions,
furniture/mounts/transformations/death, fast travel, toggle/reload and rollback.
Confirm both ordinary visibility and restoration after disabling the body experiment.

## Reproducible candidate packaging

Commit verified source changes first and use the same developer terminal:

```powershell
rustup component add rust-docs
python scripts/package.py --build-dir build --config Release --output dist --jobs 2
```

The script checks clean project/dependency revisions and metadata, performs a
clean rebuild, then verifies x64 PE/SKSE exports and the DLL's plugin version.
It creates a new version/commit-named output directory without replacing existing
files. Its player ZIP contains only the DLL/INI under `SKSE/Plugins`, a short
README with the exact source commit, and consolidated `LICENSES.txt`.
Corresponding project/dependency sources, optional symbols and hashes are separate.
Archive paths, contents and unchanged default INI are checked. Identical inputs
use deterministic ZIP metadata; binary reproducibility is not claimed.

To build the supplied sources, extract `colony-camera.tar.gz`, then extract
`dependencies.tar.gz` inside the resulting `Colony-Camera` folder. Skip dependency
fetching because those source folders have no Git metadata, and run CMake directly.
Packaging itself requires a Git checkout. The unused optional OpenVR submodule is
not part of this SE/AE build. New Rust dependencies require source/notice bundling.

This tool creates local candidates; it does not install or publish them. An
authorized binary publication must include the matching public source download
and intact notices. A local commit link is unavailable publicly until pushed.

## Performance and provenance

Sampled `PERF` logs separate first/third-person and enabled/disabled callback CPU
work from the preceding hook chain, with collision, Rust and scene stages. Every
sixteenth callback is sampled; reports include p50/p95/p99 of up to 64 sampled
own-time values. These are callback samples, not complete-frame percentiles.
The logs do not measure complete frame time, GPU body-render cost or unsampled
worst-case spikes. Instrumentation overhead has not been calibrated.
No fresh candidate FPS or compatibility benchmark is claimed. Compare native,
baseline and candidate using repeated matched routes, warmup and frame-time tails;
measure first person and third person separately.

Original project code is **GPL-3.0-or-later**. See [LICENSE](LICENSE) and
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). CommonLibSSE-NG's additional
permissions and upstream credits are retained. Other pinned dependencies include
spdlog, DirectXMath and DirectXTK; Rust runtime notices match the build toolchain.

[SmoothCam](https://github.com/mwilsnd/SkyrimSE-SmoothCam/tree/66f3960ec4de2b28af5e863c794a3924e6a2dfdd)
and [Improved Camera SE-NG](https://github.com/ArranzCNL/ImprovedCameraSE-NG/tree/2e441c190e46d96eefb7738a3276308e9c36e939)
were inspected as functional/engine references. Their implementation, translated
code, scripts, UI, assets and presets are not included. This is independently
maintained code; source inspection is disclosed without a clean-room claim.
