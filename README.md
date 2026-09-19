# Camera Colony

An independent Skyrim camera plugin with a deterministic Rust core and a C++
CommonLibSSE-NG bridge. **0.3.0 adds advanced third-person following and SmoothCam
JSON preset conversion**, alongside the optional SKSE Menu Framework interface.
The first-person placement, native weapon visibility and inventory rendering gate
from 0.2.8–0.2.10 are retained. Those behaviors were confirmed in the tester's
setup; new advanced camera behavior still requires in-game validation.
This is not a complete SmoothCam or Improved Camera replacement.

The filenames remain `ColonyCamera.dll` and `ColonyCamera.ini`.
[0.1.2 and its corresponding sources](https://github.com/MotherSphere/Colony-Camera/releases/tag/v0.1.2)
remain the published historical release; its tag and binary are unchanged.

## Graphical settings (optional)

Version 0.3.1 groups controls into aligned label/value forms, compact tabs and
collapsible sections. Offsets separates per-profile Position from global Transitions
and Advanced limits. Following separates Movement, Rotation/orbit and Vertical.
Hover labels for help; Ctrl-click sliders for precise numeric entry. Camera math,
settings ranges, presets and first-person behavior are unchanged by this UI update.

Install SKSE Menu Framework separately through your mod manager. Version 3.18 is
the integration target. Open its Mod Control Panel (F1 in the inspected default
configuration), then choose **Camera Colony**. Pages: General, Third Person,
Following, Offsets, Aiming, First Person, Legacy Profiles and Presets. Ctrl+F7 retains the native settings menu and
its diagnostics; the plugin also works without the framework.

Sliders edit a shared draft across pages. **Apply** changes the running settings;
**Apply and save** also writes ColonyCamera.ini with the existing backup policy.
**Discard edits** returns to the latest applied configuration. Defaults, saved INI
and preset imports load into the draft first. Imports retain keyboard bindings.
Presets can be exported under a chosen filename, with the previous version kept
as .bak. All controls use Rust validation; duplicate bindings and invalid ranges
are rejected before application. Legacy Profiles remain available when advanced
following is disabled.

Render callbacks never modify camera state directly. Application is queued on the
game thread; blocking framework windows suspend camera effects and hotkeys.
The API header is pinned and supplied with its LGPL license. The framework DLL
is not bundled. Native and Wine checks do not establish in-game visual compatibility;
verify navigation, Apply/save/reload and menu transitions in Skyrim.

## Advanced third person and SmoothCam presets

Enable **Advanced following** in the framework settings, or import a SmoothCam
JSON file in **Presets**, inspect the conversion report and choose **Apply**.
Existing INIs keep the legacy camera until you opt in. Enabling advanced following
changes camera behavior; it is not a switch to SmoothCam's original engine.

- Standing, walking, running, sprinting, sneaking, swimming and bow-aim groups,
  each with neutral, melee, ranged and magic variants.
- Independent world following, orbit following and vertical response. Minimum
  and maximum response fractions (at 60 Hz) vary with distance; elapsed-time
  conversion avoids tying response directly to frame rate. Zero response holds,
  one snaps, and disabling a filter follows immediately.
- Twenty-two easing curves: linear and quadratic, cubic, quartic, quintic, sine,
  circular and exponential in/out/in-out variants.
- Per-profile side, depth, height and FOV adjustments; separately timed offset,
  depth and FOV transitions. Native-relative settings retain native shoulder and
  zoom semantics. Newly imported presets use their base distance and zoom scale,
  with height applied on the world vertical axis.
- Camera-local X/Y/Z lag bounds, optional mirrored X bounds when changing shoulder,
  and downward-pitch zoom before or after orbit interpolation. Disabling a movement
  group delegates that group to the preserved legacy profiles.
- Native collision remains the final position constraint. Menu transitions,
  teleports and perspective changes discard stale camera history.

The importer reads a SmoothCam preset `{ "name": "...", "config": { ... } }`
or a direct `SmoothCam.json` object. It discovers `SmoothCam.json` and
`SmoothCamPreset*.json` under `Data/SKSE/Plugins`, plus JSON files in
`Data/SKSE/Plugins/ColonyCamera/Presets` and `Data/SKSE/Plugins/SmoothCam/Presets`. Refresh the list after adding a file.
Import reads the source without changing it, retains Camera Colony's keyboard
bindings and first-person settings, and changes only the draft. **Apply and save**
persists the conversion; exporting a `.ccpreset` preserves the resulting native
Camera Colony configuration, not unsupported SmoothCam data.

If you imported a preset with 0.3.0 or 0.3.1, import the original JSON again
and choose **Apply draft and save** to use the corrected distance/height model.
Older Colony settings retain their existing geometry until explicitly reimported.
The model and its base distance/zoom scale are editable on **Third Person**.
An inactive interpolation override means the preset uses global follow settings;
it does not disable the camera.

Expand the compatibility report to see mapped, approximate, inactive, unsupported
and unknown fields. Pathological reports stop at 1 MiB with an explicit truncation
notice; the complete input is still validated. Imported presets use
`minCameraFollowDistance` and `zoomMul` together with the native target zoom.
The follow height comes from Camera3rd, falling back to the humanoid head.
Custom follow-bone priorities and exact zoom-scroll timing are not reproduced.
This independent world/orbit solver differs from SmoothCam's interpolation
pipeline, so identical numeric values do not guarantee
identical movement. Sitting, horseback, dragon, transformed and vanity group data
can be retained in the configuration, but those camera states remain native.
Crosshair rendering, projectile aiming correction, ballistic prediction, trajectory
arcs and alternative dialogue cameras are not implemented. Custom aiming offsets
therefore require an accuracy check in game. No imported field silently enables
those missing features. Malformed JSON, duplicate keys, invalid types/ranges and
oversized files are rejected without partially changing settings.

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
conventional humanoid third-person body while retaining Skyrim's native camera.
Confirmed sheathed ordinary movement uses body arms; drawing, drawn and sheathing
weapons, readied fists or an equipped torch use native first-person arms. Unknown
weapon states also retain the native rig. Head/third-person arm masking and equipment visibility
are temporary, with restoration when the plugin still owns the changed values.
Missing or replaced skeleton nodes cause a native fallback.

User testing of 0.2.2 reports good stationary framing but body/view drift during
looking and strafing. Its raw model eye was sampled before native camera spring
and collision corrections. 0.2.3 instead uses the displayed NiCamera position
and heading after both model and camera updates. Each pass restores prior owned
output before resampling; new first-person transitions wait for a native view
update. Inactive state-update callbacks cannot restore the other perspective's
outputs. No third-person interpolation runs in first person.

Alignment now anchors the locomotion root behind the displayed camera, with
adjustable backset/lateral offset. Animated eye/head landmarks provide height and
pose diagnostics only. A full transform pass with controller advancement disabled
checks resulting world transforms, then returns temporary local values to native
values. Body scale, world height, root rotation and native camera/FOV remain unchanged.
User testing of 0.2.3 reports intermittent torso intrusion during camera turns;
small movement-only steps appear unaffected. Startup was subsequently confirmed
working. 0.2.4 fixes a demonstrated heading discontinuity: projected camera-forward
reversed beyond approximately 90.057 degrees, moving the default body backset by
24 units. Heading now consistently uses projected screen-right, with a forward
fallback only when that projection degenerates. Continuity across vertical is
verified mathematically for native views without roll; arbitrary rolling camera
rigs have no equivalent guarantee. Native actor pitch is clamped, but the final
view also adds the camera bone's orientation without another pitch clamp.
The tester subsequently reported first-person framing fixed in 0.2.4, but an
invisible drawn weapon with hands visible low/right. Disabling only the body
experiment restored the weapon. A second test kept the experiment enabled but
disabled alignment: the weapon remained invisible. Translation is therefore not
necessary for the reported symptom; these comparisons still do not establish a
specific attachment layout.
Animated torso deformation, native body tilt, height, projection and mesh clipping
remain separate; no body rotation is forced.

0.2.5 fixes a concrete runtime-access defect: inherited ActorState calls on the
player used the compile-time base layout in this multi-runtime build. Weapon-state
reads now use `AsActorState()`, as the coordinator already did. A false sheathed
reading could hide the native rig while showing body hands, and 0.2.4 also hid all
body weapon clones. The native aim fallback uses the same corrected accessor.
The candidate preserves body weapon/shield/quiver clones and keeps the native rig
available from the draw request through sheathing. The tester confirmed that
weapons are visible in 0.2.5. Head masks remain;
selected body upper arms still shrink to avoid duplicate native combat arms.

The tester reported sideways body motion while sheathed, renewed by changing or
unequipping an item in inventory. A confirmed defective 0.2.6 pose had equal view
and body-root headings (-5.7 degrees), but an eye landmark at body-local
(17.21, 28.07, 95.47). The previous algorithm could move the entire rig when this
animated landmark moved, even with matching headings. In 0.2.7, horizontal
placement depends on the restored native root instead. Changing finite eye/head
positions cannot change the resulting root placement. This fixes that mathematical
coupling; it does not establish which animation produced the reported pose or
prove that every outfit now renders correctly.

0.2.8 replaces the 0.2.7 root anchor with Improved Camera's no-headbob
landmark-distance calculation, rotated through the native body basis. It masks
unused native upper arms instead of culling the entire first-person root. Drawn
weapons still select native arms. Start with First Person > Reset alignment
(backset 8, side 0); saved settings are preserved until explicitly reset.
0.2.9 keeps body rendering active behind inventory while the native camera stays
in first person, including paused inventory. This avoids releasing the body pose
on menu opening. Equipment changes still reacquire skeleton nodes; no frozen pose
or timer is used. Other pause owners, blocking menus and camera changes retain
native fallback. Gameplay controls remain disabled by the menu as usual.
Magic, Tween and Map menus continue to suspend the body experiment.
The opening transition still requires visual confirmation with Grid Inventory.

This is a scoped port, not the complete Improved Camera renderer. Colony still
uses its rendered-camera sample and existing model/camera publication hooks;
headtracking, per-hand policies and special actions have not been ported. The
existing world-pose ownership checks do not detect every possible child-bone
writer. Test equipment changes, turning, menus and POV transitions in game before
using this candidate as a replacement for a working setup.

While the body experiment is enabled, `First-person motion window` log lines
count applied, native-fallback, not-ready and rejected publications separately
for model and camera passes. Reports are limited to one per two seconds per pass.
They include the latest rejection/fallback codes, view pitch/heading, body forward
axis and alignment sample. This covers early exits that previously bypassed the
status-change log. These are callback counts, not rendered-frame counts; a quiet
or successful report still does not prove visual correctness.
Equipment reports at the same cadence include the corrected weapon state,
native-arm policy, native root visibility/scale, equipped form IDs and native
biped attachment visibility/scale with exact body-clone alias checks. A matching
clone pointer is distinct from shared skin-bone dependencies, which these logs do
not establish.

Improved Camera's default profile is the functional target, including its default
absence of head bob. This candidate does not yet match its per-hand arm selection, head visibility,
camera/hands FOV, near-plane, scale/height adjustment or special-action policies.
Successful transform read-back proves publication, not a 1:1 visual match.
Reference inspection found camera-ownership cooperation between Improved Camera
and SmoothCam, not a generic invisible-weapon patch. Improved Camera's default
arm selection depends on equipment/actions rather than a look-up/down threshold;
its pitch-dependent near clipping is a separate feature.
Reference inspection found no ordinary real-first-person hook that forces body
yaw or adds a missing body-animation tick. Native scene-update verification also
shows both player models updating; this does not establish the reported yaw cause.

A supplied crash report from 0.2.4 strongly fits an overflowing native culling
scratch-buffer copy in the exact Skyrim executable. The report does not identify
the producer of the excessive frustum count or establish which mod caused it.
This candidate does not patch that engine function and is not a claimed crash fix.

Alignment, collision, projectile origins, interiors, head/hair/helmet clipping,
weapons, shields, torches, spell effects and shadows still require testing.
Configurable third-person arm policies, head-motion controls, independent hands FOV, near-plane adjustment and immersive
furniture/mount/transformation/death/killmove handling are not implemented.
There is no dynamic crosshair, projectile-origin reconciliation, ballistic
prediction, trajectory display or alternative dialogue cameras. Legacy aiming
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
   Proton uses the game's actual prefix. **Diagnostics > Log location** displays
   the exact path selected by the running plugin.

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

**Diagnostics > Body facing** shows the last available gameplay pose captured
before the settings menu opens, including view/body headings and the body's local
eye landmark. Status and Body facing use one camera-view record, rather than mixing
model and camera publications. It is a snapshot, not a live view while the menu is open. Open it after
reproducing an offset and compare with a centered pose; **Diagnostics > Log location**
provides the active log path separately. These pages allow reporting measurements
without first locating the log file, including under Proton.

Numeric fields use Increase/Decrease and ten-step buttons. Changes apply in
memory immediately. **General > Save settings** persists them by flushing a
temporary file, then replacing the INI while keeping its previous version as
`ColonyCamera.ini.bak`. Saving writes the canonical format; old comments remain
in the backup. Reload rejects malformed files and retains active settings.
Closing the menu alone does not save changes. Shoulder choice is session-only.

INI format 5 accepts formats 1, 2, 3 and 4, preserving bindings and the existing
first-person enabled state. Missing `[third_person] enabled` defaults to true.
The third-person switch affects only that perspective; first person retains its
own switch. Ctrl+F8 / General > Toggle effect remains the master switch for both.
Missing alignment fields use the new defaults;
the body experiment remains off in a fresh configuration. Missing locomotion
sections remain inactive. Missing `offset_half_life` inherits an explicitly supplied
legacy `half_life`. Existing key remaps are retained; if an older binding already
uses F7, an unused menu key is chosen. Unknown/duplicate fields, conflicting keys,
non-finite numbers and out-of-range values reject the entire file.

First Person provides body/alignment toggles and numeric backward/lateral controls.
`alignment_enabled=true` requests reference no-headbob placement. `body_backset=8`
is an additional backward offset (0-40), not an absolute camera-to-body distance.
`body_side=0` adds lateral offset (-20 to 20; positive is body-right). Offsets are
scaled by skeleton scale. The native body basis is preserved, so a tilted basis
can produce a vertical component. Missing eyes, non-rigid transforms or excessive
displacements fall back to native placement. Disable alignment to compare native
placement while keeping body rendering enabled. Save persists settings. The
camera is not moved; unused native arm bones are temporarily scaled, then restored.

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

Legacy half-lives use exponential interpolation. The advanced engine has its own
curve and duration settings; the two engines are mutually exclusive.
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
Cross-compilation builds Windows executables; Wine tests use an isolated prefix.
Neither compilation nor Wine substitutes for a Skyrim gameplay check.

Validate entry bytes, Address Library mappings and camera RTTI against a
legitimate local game installation:

```powershell
python scripts/verify-runtime.py "<game>/SkyrimSE.exe" "<game>/Data/SKSE/Plugins/versionlib-1-7-104-0.bin"
```

The checks cover the cross-language ABI, configuration migration and atomic
persistence, camera ownership, first-person body placement, render transforms,
advanced interpolation/transition behavior and strict preset conversion. Runtime
verification covers 14 entries, two camera RTTI tables and both guarded callsites.
Compilation and static export checks cannot verify rendering or compatibility.
The existing whole-scene publication/restore mechanism is retained: replacing
only node world transforms would omit native flattened-bone/skinning caches.
Interactions with partial updates by other scene producers require in-game checks.

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
not part of this SE/AE build. Also extract `rust-crates.tar.gz` into the same folder, including its hidden
`.cargo/config.toml`; locked Rust crates then build offline from `vendor/`.
Rust sources and their complete notices are bundled automatically.

This tool creates local candidates; it does not install or publish them. An
authorized binary publication must include the matching public source download
and intact notices. A local commit link is unavailable publicly until pushed.

## Performance and provenance

Sampled `PERF` logs separate first/third-person and enabled/disabled callback CPU
work from the preceding hook chain, with collision, Rust and scene stages. Every
sixteenth callback is sampled; reports include p50/p95/p99 of up to 64 sampled
own-time values. First-person model and final-view callbacks are reported in
separate stages with their preceding chains; third-person samples cover the
camera-state callback. These
are callback samples, not complete-frame percentiles or comparable stage costs.
Since 0.3.3, third-person `rust` times only the Rust call; earlier versions also
included input preparation and native INI lookup. Compare `own` across those
versions rather than attributing that instrumentation change to a Rust speedup.
The minimum-zoom setting is resolved once at data load and its live value is read
per frame, avoiding repeated linear INI searches without freezing the setting.
Since 0.3.4, `PERF_BODY` breaks first-person samples down into restoration,
preparation, named-node lookup, full body updates and native-arm updates. It also
reports average update and native-arm lookup counts per sampled callback.
Restoration/preparation include nested work: do not add these fields together.
Timing scopes read the clock only on sampled callbacks; their overhead is included
in `own`. Preparation includes validation and camera/actor inputs, not just checks.
Native arm nodes are cached while attached to the current root; root replacement
or detached/missing nodes triggers a new lookup. Both body publication passes and
all transform/ownership checks remain in place.
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
are credited references. `src/first_person.rs` adapts Improved Camera's
`AdjustModelPosition(false)` / `TranslateThirdPersonModel` placement under MPL-2.0,
with additional GPL-3.0-or-later availability for this combined work under MPL 3.3.
The original MPL text is in `licenses/ImprovedCamera/MPL-2.0.txt` and included in
the player package notices. No upstream binary, scripts, UI, assets or presets
are supplied. See THIRD-PARTY-NOTICES.md for exact provenance.
