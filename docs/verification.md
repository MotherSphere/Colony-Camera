# Colony Camera verification

This is a historical record. Each section describes the evidence available at
that stage, not a claim that all later versions received the same validation.

## Historical: 0.1 alpha, before the user's test

Date: September 17, 2026. No in-game test had been performed. The Eidos
installation was unchanged.

### Checks performed

- Six Rust tests: convergence and independence at 30/60/144 FPS, maximum lag,
  teleports, long pauses, rotated offsets, invalid input, atomic configuration,
  duplicate entries, out-of-range values and the reserved Ctrl modifier.
- `cargo clippy --all-targets -- -D warnings` and `cargo fmt --check`.
- Full CommonLib and plugin compilation for Windows x64 with clang-cl, the xwin
  SDK/CRT and the Rust MSVC core. No third-party Rust dependencies.
- `camera_abi.exe` under Wine: structures passed by value, valid/invalid
  configuration, unchanged output on failure, null-pointer rejection and mutation
  testing of every byte in the four binary guards.
- `camera_load.exe` under Wine: actual DLL and dependency loading, presence of
  SKSEPlugin_Load / SKSEPlugin_Version exports and clean rejection of a null SKSE
  interface. This does not simulate SKSE loading inside Skyrim.
- Read-only checks of SkyrimSE.exe and Address Library: Begin/End/Update slots
  1/2/3 in vtable 205236 and collision entry 50832. Binary prefixes matched. In
  0.1, those guards were also applied before installing hooks.

Hashes of the game files read during verification:

- SkyrimSE.exe: `846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f`
- Address Library: `8aab3dd251d135b849bd983f86a4a205c920fa3e81f8e30c0e63ccfef9423842`

The build emitted warnings in CommonLib headers. Wine emitted Mesa warnings in
this non-rendering test prefix; both verification executables exited with code 0.

### Review

Independent review found missing restoration on End, conflict checks performed
too early and the ability to bind Ctrl alone to an action. Fixes added restoration
on End and Begin, rechecking at PostPostLoad/DataLoaded, and rejection of codes
29/157. Restoration precedes reset and runs only when the object and position
still exactly match Colony Camera's last applied output. It does not overwrite a
position already changed by the engine. The native aiming profile does not run a
second collision. Invalid input invalidates the Rust result and leaves the native
position intact.

### Limitations and required testing at this stage

The ABI and prefixes do not prove the complete semantics of CheckCameraCollision
after filtering. The accepted position is fed back to the filter, but moving
obstacles, corners and stairs require actual gameplay tests. Engine ordering,
local-axis direction and shoulder inversion also require visual confirmation.
There were no in-game performance measurements at this stage.

Conflict checks covered SmoothCam and the monitored native/vtable entries during
loading. Plugins changing the camera elsewhere or later could still conflict.
No comprehensive compatibility guarantee was made.

Before stable publication, test save loading, walking/running/turning, walls and
narrow passages, first/third-person transitions, dialogue, inventory and console,
cell changes, fast travel, death, mounts, bows/crossbows and magic, shoulder
switching, toggling and INI reloads. Watch the aiming crosshair and compare FPS
with the effect disabled. Keep the DLL and its matching PDB when collecting a
crash log.

### Remaining features

MCM, custom crosshair and trajectory rendering, tested inter-mod compatibility,
multiple runtimes and advanced presets. This alpha is not a complete replacement
for every SmoothCam feature.

### Delivered package

`Colony Camera 0.1 alpha.zip` was built from `76389e0`, containing the DLL, INI,
notices and project/dependency sources. The ZIP and each SHA256 manifest entry
were verified after copying to the Desktop.
ZIP SHA256: `b25f40179bbeec7a70b79ff998c3d065f2675f5452dadfc1260dcff5870a3940`.
The matching PDB was retained beside the ZIP, outside the mod installation.
No Eidos installation, remote publication or Nexus upload occurred at that stage.

## 0.1.1 fix — September 17, 2026

The user's 0.1 test confirmed DLL loading but no visible effect: logs showed
processing stopped when entry points changed. Improved Camera hooks the collision
entry; rejection also persisted without that mod. The second trigger was not
identified. The runtime-guard claims above describe 0.1, not 0.1.1.

The fix installs at PostLoadGame/NewGame, chains existing slots, validates
executable addresses without demanding unmodified bytes, publishes root and
NiCamera positions, and recalculates the matrix through AE 70641. That entry was
read from the actual executable and added to the offline verifier, bringing it
to five entries. Improved Camera's collision hooks remain in the call path; no
hook is bypassed through a vanilla address captured before installation.

The six Rust tests, Clippy, formatting, C++ ABI and Wine DLL loading passed. The
C++ publication test checks four positions, preserves the NiCamera child's own
offset and verifies that repeated identical publication does not accumulate
movement. It failed before implementation because the helper was absent, then
passed. Skyrim startup, collision and Improved Camera animations still required
in-game testing.

Review confirmed chaining, the collision signature and writes, and render
publication. It found an overly broad zoom exclusion: negative zoom values remain
valid in third person. The threshold was removed; no native zoom range is
arbitrarily excluded.

### Delivery

Package commit: `e94c5d0`, local branch `camera-initiale`, with no push or merge at
that time. DLL 0.1.1 was installed with Skyrim closed into the existing Eidos
`Colony Camera 0.1 alpha/SKSE/Plugins` folder. The INI remained byte-identical.
DLL/INI/log backup: `dist/backups/20260917-005754`.
Improved Camera remained enabled and SmoothCam disabled.
Installed DLL SHA256: `2458f6c78d52c02e2645f0286736ff52e2ad63974a9fb6ed913c05395a691794`.
The 0.1.1 ZIP and PDB were copied to the Desktop folder `Mods créés/Colony Camera`,
without overwriting 0.1. ZIP SHA256:
`07014b30f51e012a319167ae37aa4d3e17c64a07e787bd24c379c693dad44cb1`.
No in-game 0.1.1 log was available yet.

## 0.1.2 fix — September 17, 2026

The user's 01:06–01:08 log showed successful loading, Begin chaining to TDM,
End/Update chaining to SkyParkour and callback execution, followed by rejection:
"Camera render node missing or root parented". No displacement was confirmed.
SKSE showed Improved Camera loaded despite our false negative; its filename is
`ImprovedCamera.dll`.

Investigation found NiCamera's GetRTTI in vtable AE 237191 at 0x140EF1A50. Its LEA
instruction returns 0x14331C690, matching AE 410506 expected by netimmerse_cast.
This ruled out an incorrect CommonLib identifier for the native type, but did
not establish that session's scene structure. Skyrim was no longer running, so
the live scene could not be inspected.

The unconditional parent rejection was removed. World-to-local conversion uses
NiTransform::Invert, with atomic publication only for finite positions. C++ tests
cover a parent translated by (100,200,300), rotated 90 degrees and scaled by 2:
world (80,240,360) must produce local (20,10,30), preserving the child offset.
Zero scale is rejected without writes. The test was introduced before parent
support, failed compilation because the argument was absent, then ran under Wine.
The Windows build, six Rust tests, Clippy and formatting passed; DLL loading and
the ABI were checked under Wine. In-game validation was still pending then.

If NiCamera is absent, the new diagnostics report the parent, child-slot count
and types. The parent hypothesis must not be treated as an observed fact in the
0.1.1 session: its diagnostic combined two possible causes.

Independent review found no blocking defect. A suggested additional test was
added: a parent with NaN translation is rejected after calculation, comparing all
twelve position components before and after to rule out partial writes.

Delivery from `f3a1e9b`: DLL replaced in the existing Eidos folder with the game
closed and the INI unchanged; backup `dist/backups/20260917-011459`.
Verified receipt: `dist/installation-0.1.2.json`. ZIP/PDB copied to the Desktop,
older versions retained. ZIP SHA256:
`3608ef9284a095ae50bb005854928e3faeb6a1468493e3d0d68137c030285405`.
No push, merge or Skyrim launch was performed during delivery. Another user test
was required.

## 0.1.3 diagnostic — reported slowdown

The user confirmed a visible effect in 0.1.2. The 01:16:53 log recorded a parent
and an applied position, followed by nonzero displacement at 01:16:56. The scene
blocker was therefore resolved in that session. The user then reported a drop
from 200 to 170 FPS while running and turning, absent with Ctrl+F8 off. Those
rates correspond to approximately 5 ms and 5.88 ms per frame.

Sampled Update instrumentation measures total time minus the previous callback,
second collision, Rust calculation and scene/matrix publication. Enabled and
disabled samples are separate, reporting every 64 samples from one frame in
sixteen. This is diagnostic instrumentation, not an optimization or an FPS/GPU
measurement. Small costs before the probe and log output are excluded from `own`.

A C++ test was added before the helper to check previous-chain exclusion,
averages, applied-frame counts, maximum and reset using synthetic durations.
The Windows build, six Rust tests, Clippy, formatting and Wine ABI/loading tests
passed. Camera positions, smoothing and collisions were unchanged. The next step
at that stage was to read the A/B PERF reports before choosing an optimization.
