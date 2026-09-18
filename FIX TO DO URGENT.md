# URGENT: first-person body placement and rendering handoff

**Status: unresolved visually; 0.2.7 contains a targeted correction awaiting game testing.**

This is the complete project and defect handoff for the `integrated-camera`
branch. It records observations, verified defects, attempted fixes, remaining
risks and acceptance criteria. Do not close the visual issue on the strength of
a successful build, transform read-back or mathematical test alone.

Prepared on 2026-09-18. The last compiled gameplay candidate is **0.2.7**, commit
`297380288db4a0979f1e1f8039cdf903eda150ea`. This document is a subsequent
documentation change; it does not imply another binary build. The preceding
whole-project audit examined **0.2.6**, commit
`e72036f6db281de9413987b2f637afa79bace7eb`. Its findings below are updated to
distinguish changes already made in 0.2.7 from outstanding work.

## 1. Project purpose, scope and current implementation

Camera Colony is an independent Skyrim SKSE camera plugin. Its long-term target
is configurable third-person behavior comparable to SmoothCam, together with
first-person body presence comparable to **Improved Camera's Default profile**.
The functional target is not a claim of feature parity or a request to copy the
reference implementations. No SmoothCam or Improved Camera code, binaries,
presets or assets are included in this branch.

The plugin ships as `ColonyCamera.dll` and `ColonyCamera.ini`. It uses a
deterministic Rust core for state selection, validation and camera/body math,
with a C++23 CommonLibSSE-NG bridge to Skyrim. The project is GPL-3.0-or-later;
native dependencies and their notices are recorded in
[dependencies.json](dependencies.json) and
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). The Rust core has no external
crate dependency. Native dependencies are pinned to exact revisions.

The supported target for this candidate is **Steam Skyrim 1.7.104.0, SKSE 2.3.1
and the matching Address Library**. Other game runtimes, GOG and VR are not
supported. The C++ bridge checks runtime identity and expected hook targets
before installing the affected hooks. Linux/Steam Deck testing runs the Windows
plugin under the tester's Proton environment; native Linux support is not implied.

### Implemented behavior

- Third-person time-based following with bounded lag, configurable camera-space
  offsets, shoulder switching, extra depth, world FOV and independent transition
  half-lives. Native collision runs after the requested movement.
- Exploration, combat and aiming profiles, with optional swimming, sneaking,
  sprinting and airborne overrides. Profile precedence is aim, swimming,
  sneaking, sprinting, airborne, combat, exploration. Locomotion overrides are
  disabled in the supplied defaults.
- A coordinator chooses native, third-person or first-person ownership.
  Ordinary first-person does not run the third-person interpolation step.
- An **opt-in first-person body experiment, disabled by default**. It displays
  the conventional humanoid third-person body while preserving the native
  first-person camera. It masks the head and selects body/native arms according
  to weapon/light state; unsupported or unavailable scene data falls back.
- Native modal settings: General, Third Person, First Person, Aiming, Presets,
  Compatibility and Diagnostics. No ESP, Papyrus scripts, SkyUI or external UI
  assets are required. Controls are Ctrl+F7 settings, Ctrl+F8 master toggle,
  Ctrl+F9 shoulder and Ctrl+F10 reload, with configurable scan codes.
- Versioned INI validation/migration, explicit save with backup, independent
  perspective switches and own-format presets. These are not SmoothCam/IC
  preset imports. The plugin does not write camera configuration into saves.
- First-person publication, weapon and pose diagnostics, including the active
  log path and a frozen pre-menu body-facing sample.
- Clean committed-source candidate packaging with player, corresponding-source
  and symbol archives, license notices and SHA-256 manifests.

### Important missing behavior

This branch is a development candidate, not a complete replacement for either
reference mod. It lacks per-hand arm policies, configurable head-motion policy,
first-person height/scale adaptation, independent hands FOV, first-person near
plane policy, validated effects/shadows and immersive special-action states.
Furniture, mounts, transformations, ragdoll, death and killmoves use native
fallbacks; first-person swimming/airborne also fall back. Native fallback is not
immersive support for those actions.

There is no dynamic crosshair, projectile-origin reconciliation, ballistic
prediction or trajectory display. Default aiming preserves the native camera;
custom aiming offsets can reduce accuracy. Camera orientation itself is not
smoothed. All body/skeleton, animation, TDM, SkyParkour, ENB and Community Shaders
compatibility claims require their own tests.

`SmoothCam.dll` detection prevents Colony camera hooks from installing.
`ImprovedCamera.dll` / `ImprovedCameraSE.dll` detection disables Colony's body
experiment, but does not implement Improved Camera's control-transfer protocol
for simulated first-person states. Detecting known DLL names is not exhaustive
camera-provider detection. Use Colony alone for the initial reproduction.

## 2. User-visible defect and expected result

The tester reports that stationary first-person framing can look correct, but
turning the view left/right, especially while looking downward, makes the body
appear to orbit, rotate or move sideways relative to the camera. The torso can
enter the view as though the camera were inside the body. In one reported state,
stopping the mouse did not recenter it; turning in the opposite direction did.

The tester initially suspected Colony's third-person smoothing leaking into
first person. Later reports associate the defect mainly with **sheathed weapons**;
changing or unequipping an item through inventory made it fully reproducible
again. Small movement-only steps were reported as less problematic than camera
motion. These are observations, not proof of a particular animation producer or
a third-person ownership leak.

Expected ordinary first-person behavior: the view remains a coherent player POV
while looking, walking, turning and changing equipment. The body should remain
naturally positioned beneath the view without a persistent sideways orbit or
torso intrusion caused by the plugin. Hands, drawn weapons and equipment must
remain visible and consistent. Returning from inventory or switching perspective
must not preserve a stale body offset. Disabling the experiment must restore the
native presentation without a residual scale, visibility or position change.

The intended baseline is Improved Camera's **Default** behavior without default
head bob. This does not mean forcing all character/torso animation rigid or
inventing an angle-triggered arm transition that the reference does not have.

## 3. Reproduction and test environment

Known tester environment: Skyrim 1.7.104 under **Linux / Steam Deck with Proton**.
The exact Proton version, full load order, skeleton, body/armor, animation stack,
weapon, menu-preview mods and active INI values are not fully established in the
available evidence. Do not fill these gaps with assumptions.

1. Install one Colony candidate with the matching runtime dependencies. Disable
   competing camera providers for the baseline and identify the loaded version
   in Diagnostics. Preserve the existing INI and saves.
2. Enable First Person > Body experiment and Alignment. For **0.2.7**, select
   **Reset alignment**: backset 12, sideways 0. These numbers now refer to the
   body root, so old eye-based tuning is not an equivalent comparison.
3. In the same scene and outfit, start with a centered stationary first-person
   pose. Look down at the body, then turn left/right slowly, quickly and through
   reversals. Stop the mouse and observe whether the body stays displaced.
4. Compare movement-only steps, movement plus turning, and standing turns.
5. Open inventory, change weapons or unequip, close it, and repeat the turns.
   Repeat with weapon sheathed, drawing, drawn and sheathing.
6. Compare Alignment off while keeping Body experiment on, then disable only
   Body experiment. Repeat perspective switching and verify restoration.
7. Capture centered and defective gameplay views, plus **Diagnostics > Body
   facing** immediately after each. Use **Diagnostics > Log location** for the
   active log path rather than assuming a host Windows Documents directory.

Under Proton, Documents lives in the game's actual prefix and may be redirected.
The tester could not find the initially suggested Windows log path. The in-game
path display and diagnostic pages were added to avoid blocking on that location.
Diagnostic menus show a frozen pre-menu sample; the body behind an open modal
menu has already undergone restoration and is not a valid before/after capture.

## 4. Full sequence of observations and fixes

| Candidate / commit | Change or observation | What remains established |
| --- | --- | --- |
| 0.2.0 / `d07b1f9` through `0ed3063` | Integrated coordinator, settings, opt-in body rendering, validation, packaging and interpolation bounds. | Foundation only; no complete IC/SC parity. |
| 0.2.1 / `e729431` | Added configurable horizontal placement behind the native eye. | Body became visible, but proportions/framing and camera movement remained wrong. |
| 0.2.2 / `af9391a` | Published body transforms after native model updates and checked resulting world transforms. | Tester reported good stationary framing but looking/strafing drift and camera entering/exiting the body. |
| 0.2.3 / `04f99eb` | Reconciled against the final rendered camera, isolated perspective controls and waited for valid native first-person updates. | Startup was confirmed working after an initial false alarm. Torso intrusion during camera turns remained; small movement-only steps seemed less affected. |
| 0.2.4 / `2689e2d` | Fixed a proven horizontal-heading discontinuity through near-vertical views by using projected screen-right, with guarded fallback. | Tester described first-person framing as fixed, then reported invisible drawn weapons and hands low/right. A separate crash was reported on this version. |
| 0.2.5 / `8a83001` | Corrected runtime weapon-state access through `AsActorState()` and preserved body weapon/shield/quiver clones plus the native combat rig. | Tester confirmed the weapon visible. Sideways body motion persisted, mainly sheathed and aggravated by inventory changes. |
| 0.2.6 / `e72036f` | Added in-game body-facing snapshot and actual log location. | Tester confirmed diagnostic screenshots were taken during the through-body defect. These narrowed the cause; they did not resolve it. |
| 0.2.7 / `2973802` | Anchored horizontal placement to the restored native body root, added explicit preview-menu gates and unified the diagnostic snapshot. | Compiled and packaged; **no tester confirmation of the visual outcome yet**. |

Some intermediate trial settings/builds were described as worse, including being
fully inside the body. Their exact binary identity and complete configuration
are not recoverable from those descriptions. Do not assign an unverified trial
result to a specific committed change.

The tester remembers a correct body rotation before the weapon fix and a later
regression. Review did **not** find that the 0.2.4 heading correction was removed.
Later visibility policies expose different geometry, which can change the
appearance without reverting that math. The reported timing is useful evidence,
but does not prove a particular regression line.

### The missing-weapon discriminator

- Body experiment off: the weapon reappeared with Alignment still requested.
- Body experiment on, Alignment off: the weapon remained invisible.
- Therefore translation was not necessary for that missing-weapon symptom.
- A concrete multi-runtime layout defect was subsequently found: inherited
  ActorState access on the player did not use the runtime-aware base offset.
  `AsActorState()` now supplies weapon-state reads, including native aiming.
- The old policy also hid body weapon clones. Current policy preserves them and
  retains native first-person arms from draw request through sheathing, for
  unknown states and for an equipped light. Confirmed ordinary sheathed movement
  uses body arms. The tester confirmed weapon visibility after these changes.

Do not revert this accessor or blanket-hide equipment to address body placement.

## 5. Captured defective pose: facts and limits

The tester explicitly confirmed the following **0.2.6** screenshots were taken
while reproducing the through-body problem.

| Display | Recorded value |
| --- | --- |
| Body facing: view yaw | -5.7 degrees |
| Body facing: body-root yaw | -5.7 degrees |
| Body facing: body minus view | -0.0 degrees |
| Body facing: eye in body coordinates | (17.21, 28.07, 95.47) |
| Body facing: sample age at menu opening | 0.01 seconds |
| Body facing: scene status | Applied |
| Status: runtime / owner / state | 1.7.104.0 / Body experiment / Active |
| Status: first-person hooks / competing provider | true / false |
| Status: scale / horizontal translation | 1.03 / (-15.1, -45.1) |
| Status: eye/head height difference | 0.3 |
| Status: raw-to-rendered view correction | approximately (0, 0, 3.08) |
| Status: position / scale publication error | 0.000 / 0.000000 at displayed precision |

The defective body-facing sample exists with matching horizontal **root** and
view directions. Adding an unconditional root-yaw lock is not justified by that
sample. It does not prove full 3D agreement, correct animated torso/neck pose or
that no other frame ever has a heading mismatch.

In 0.2.6, Body facing and Status could describe different publication phases.
The yaw and local-eye fields within Body facing are one sample, but combining
them with Status translation/scale as an exact simultaneous reconstruction is
not justified. 0.2.7 fixes that diagnostic inconsistency with `BodyViewSnapshot`.

`Third-person camera: true` is an enabled setting, not the current camera owner.
Code inspection confirms the Rust third-person step is not called in ordinary
first person. `Applied` / `verified` prove that measured transforms matched the
requested publication, not that the requested pose rendered correctly.

## 6. Why 0.2.7 changes the anchor

Before 0.2.7, horizontal placement used an animated eye landmark, or the head
fallback, to move the entire body. With camera C, animated anchor A, body point T,
scale s, configured backset b, sideways offset q and horizontal view axes F/R:

```text
old_delta_xy = C.xy - A.xy - s*b*F + s*q*R
displayed_T.xy - C.xy = T.xy - A.xy - s*b*F + s*q*R
```

Even with matching body/view yaw, animation of A relative to the torso changes
the displayed whole-body placement. Successful eye alignment therefore did not
guarantee a stable torso. The captured off-center landmark is relevant evidence,
but does not identify which animation, head tracking or equipment operation
produced it.

0.2.7 samples the **restored native locomotion root B** before writing changes:

```text
new_delta_xy = C.xy - B.xy - s*b*F + s*q*R
new_delta_z = 0
```

Eye/head positions now supply pose and height diagnostics, not the horizontal
anchor. Finite eye/head motion cannot move the whole root through this equation.
The native body scale, world height, root rotation and first-person camera/FOV
are preserved. Animated torso deformation, native tilt, projection and mesh
intersection remain separate possible problems.

The C ABI `BodyAlignmentFrame` gained `body_root[3]`: size is **64 bytes**, root
offset **52**. The configuration layout remains **324 bytes**, INI format **4**.
Tests cover the new ABI, the reported off-center-eye scenario, independently
moving head/eye landmarks, translated/scaled roots and heading changes. Synthetic
fixtures do not exercise actual engine animation, skinning or menu ordering.

Inventory, Magic, Tween and Map explicitly suspend the body experiment.
Menu-open cleanup is queued on the SKSE main thread, rechecks whether the menu
is still open and resets retained first-person state. Eligibility also checks
fresh menu state on every publication. This covers a previously missing boundary
for non-pausing menus; it is **not proof** cleanup occurs before every possible
third-party preview draw. There is no persistent menu latch or calibration pose.

## 7. Source map and lifecycle

| Source | Responsibility / review focus |
| --- | --- |
| [src/lib.rs](src/lib.rs) | Rust third-person step, bounded history, interpolation, FOV and C exports. |
| [src/config.rs](src/config.rs) | Defaults, strict validation, INI migration/parsing and serialization. |
| [src/coordinator.rs](src/coordinator.rs) | Owner selection, unsupported-state fallback and profile precedence. |
| [src/first_person.rs](src/first_person.rs) | `align_body`, stable root placement, diagnostics and bounded inputs. |
| [plugin/main.cpp](plugin/main.cpp) | `Coordinate`, `NativeAim`, camera hooks, `PublishFirstPersonBody`, `BodySceneUpdate`, `CameraViewUpdate`, menu events and `BodyViewSnapshot`. |
| [plugin/first_person.h](plugin/first_person.h) | `Renderer::Apply/Restore/Reset`, node lifetime, masks, local leases and native world publication. |
| [plugin/body_position.h](plugin/body_position.h) | Native/body arm policy, hierarchy/coherence checks, world-to-local displacement and publication validation. |
| [plugin/body_facing.h](plugin/body_facing.h) | Stable horizontal heading and body/view diagnostics. |
| [plugin/scene.h](plugin/scene.h), [plugin/owned_value.h](plugin/owned_value.h) | Third-person publication and conditional restoration of owned values. |
| [plugin/core.h](plugin/core.h), [plugin/runtime.h](plugin/runtime.h), [plugin/hook_validation.h](plugin/hook_validation.h) | ABI, exact supported runtime and guarded hook targets. |
| [plugin/settings_menu.h](plugin/settings_menu.h), [plugin/config_store.h](plugin/config_store.h), [plugin/timing.h](plugin/timing.h) | UI, persistence and sampled callback timings, not an FPS/GPU benchmark. |
| [tests](tests), [scripts](scripts), [CMakeLists.txt](CMakeLists.txt) | Math/lifecycle helpers, ABI and package tests, runtime verification and builds. |

Ordinary 3P restores prior owned outputs, invokes the native chain, rechecks
eligibility, computes Rust motion, applies collision, validates and publishes.
Inactive state callbacks must not restore or overwrite the other perspective.

The first-person model hook runs after the native body model update. The final
camera hook restores the prior body publication before the native solver, waits
for an actual first-person update and republishes against the rendered view.
Each body apply restores its previous lease before sampling fresh native data.
Temporary local values are returned immediately after propagation; published
world data is retained only under the existing ownership checks.

Native propagation affects more than node `world` transforms: inspection of the
exact runtime found previous-world values, frame/flag metadata and
`BSFlattenedBoneTree` entries, including bones without their own NiAVObject.
Replacing restoration with a `world/worldBound` snapshot alone is incomplete.
Do not make that substitution without a supported cache/lifecycle contract.

## 8. Remaining urgent work and other audit findings

These are ordered investigation items, not claims that every conditional risk
occurred on the tester's machine.

| Priority / item | Current status and required work |
| --- | --- |
| **Urgent: visual body stability** | Validate 0.2.7 against the reproduction matrix. If defective, compare native torso/head/pelvis pose, root placement and final view in the same phase. Do not substitute transform verification for visual success. |
| **P1 conditional: whole-skeleton restoration ownership (F1)** | Still open. Root, head and two upper-arm transforms plus parent identity do not establish ownership of every descendant/cache touched by a full update. Another producer can change only an untracked child or a subset of representations. Define a coherent phase and abandonment/recompute policy; cover partial writers and equipment replacement. |
| **P2: combat default mismatch** | Still open. Shipped INI `max_lag=30` differs from compiled combat reset/default `60`. Choose a single intended value and compare the actual shipped configuration against it. |
| **P2 conditional: non-rigid parent transforms** | Still open. Finite matrix admission does not prove orthogonality; transpose is used as inverse. A sheared basis can turn a horizontal request into vertical movement. Reject unsupported bases or implement a justified inverse, with shear/singular tests. No such tester skeleton is proven. |
| **P2 conditional: ancestor coherence** | Still open. Checking each tracked bone against its immediate parent does not prove a fresh coherent neck/spine/root chain. Establish the relevant generation/chain contract. |
| **P2: arm/visibility policy granularity** | Still open. Ordinary sheathed mode culls the whole native 1P root rather than selecting visibility per hand. Camera/attachment/effect behavior needs a specific contract. Native-root culling has **not** been proven to freeze the camera. Preserve the weapon fix. |
| **P2: menu/equipment boundaries** | Code-level exclusion and queued cleanup added in 0.2.7. Actual native and modded preview timing, root replacement and post-inventory rendering still require game validation. |
| **P2 conditional: third-person restoration provenance** | Still open. Local, world and rendered positions have independent leases without captured parent identity. Reparenting or a partial external write can produce a mixed restored pose. Separate from the reported ordinary-1P defect. |
| **P2 conditional: IC coexistence** | Still open. Disabling Colony body hooks does not implement request/release ownership in IC fake-first-person states that retain a native ThirdPerson state. Validate/restrict coexistence or implement a verified protocol. The defective screenshot reported no competing provider. |
| **P3: large-coordinate 3P arithmetic** | Still open. `rendered + position - world` can lose precision; compute the displacement before adding it. A synthetic counterexample exists; no link to this 1P symptom is established. |
| **P3: cross-phase diagnostics** | Addressed in 0.2.7 with one final-camera snapshot; retain clear sample availability/age semantics. |
| **P3: ABI heading comment** | Corrected in 0.2.7 to describe rendered-view heading, not skeleton heading. |
| **Coverage gap: engine lifecycle tests** | Still open. Pure helper tests do not exercise real `Renderer::Apply/Restore`, native skinning caches, menu ordering and competing scene producers. Add meaningful lifecycle fixtures and validate in the game. |

## 9. SmoothCam and Improved Camera reference findings

Reference inspection used fixed revisions, not an assertion about every current
version of those projects:

- [SmoothCam, commit 66f3960ec4de2b28af5e863c794a3924e6a2dfdd](https://github.com/mwilsnd/SkyrimSE-SmoothCam/tree/66f3960ec4de2b28af5e863c794a3924e6a2dfdd).
- [Improved Camera, commit 2e441c190e46d96eefb7738a3276308e9c36e939](https://github.com/ArranzCNL/ImprovedCameraSE-NG/tree/2e441c190e46d96eefb7738a3276308e9c36e939).

SmoothCam separates target following, local movement, transitions, collision and
aiming. Its control request/release API is used by IC in selected simulated
first-person contexts. This is ownership cooperation, not a generic patch for
invisible weapons. Colony's ordinary first-person path is independently selected;
no direct third-person interpolation leak was found in that path.

The inspected IC Default profile disables head bob, specifies world FOV 80 and
hands FOV 65, and uses an ordinary near plane of 15 with a look-down branch at 4.
Its arm choices depend on equipment, hands and actions; looking down is **not** a
universal switch between arm rigs. Near clipping and arm visibility are separate.
IC also has height/scale, look-target, head/equipment, effects/shadow and special
camera-state policies that Colony does not reproduce.

No ordinary real-first-person IC hook was found that justifies an unconditional
body-yaw overwrite or a supposedly missing body-animation tick in Colony. Native
inspection found both player models updating. The IC revision studied does not
target Skyrim 1.7.104: its offsets/hooks cannot be transplanted into this runtime.
Several declared HIDE settings lacked an identified engine consumer in that
revision; an INI declaration alone is not evidence of implemented behavior.

The 0.2.6 Colony audit covered all **36 project-owned source/test/build files
(6,216 physical lines)** and inventoried 60 tracked files. Reference reviews
focused on camera/state/hook/configuration/visibility/ownership paths; they were
not exhaustive audits of every dependency, binary asset or development UI.
Those historical counts do not describe the size of the later 0.2.7 diff.

## 10. Separate crash report and unrelated tool error

A supplied CrashLogger report dated 2026-09-18 01:11:51 was identified by the
tester as **0.2.4**; the action immediately before it was unknown. It reports an
execute access violation at `0x000000030000003F` after about 3m35s. Scanned stack
entries are not a reliable unwound call stack, and module presence does not
attribute causality.

Read-only inspection of the matching 1.7.104 executable found an unbounded
active-plane-state copy fitting an overflow of a native culling scratch buffer:
`BSCullingProcess::Process1` at RVA `FEDA70`, copying through the helper at
`FF91E0`. The reported register/stack pattern strongly fits a 260-entry copy
overwriting saved state/return data. Missing live bytes, original overwritten
values and compound-frustum state prevent a conclusive reconstruction.

The producer of the excessive count, any runtime patch and any contribution by
Colony remain unknown. The body fix does not patch that engine function and must
not be described as a crash fix. Native biped getter direction, array stride and
clone-field access were checked; those layouts were not shown to be the cause.

A separate DynDOLOD screenshot names deleted references/cell resolution in
`[Rudolph] Dark Souls.esp`. This is a distinct plugin-data report, not evidence
that the camera DLL caused the body defect or that it needs a camera-code patch.

## 11. Validation actually performed for 0.2.7

| Check | Result / limitation |
| --- | --- |
| `cargo fmt --check` | Passed. |
| `cargo clippy --locked --all-targets -- -D warnings` | Passed. |
| `cargo test --locked --no-run` | Test executables compiled. |
| `cargo test --locked` | **Blocked before the first executable launched**, Windows Application Control error 4551. The 49 Rust tests are not recorded as passed. |
| Python packaging tests | 10 passed. These include synthetic PE fixtures; they are not game-load tests. |
| Clean Windows Release build | Passed, 536 steps; x64 DLL and eight native test executables built. Native tests were not launched after the confirmed application-control block. |
| Runtime verification | 14 entry prefixes, two guarded callsites and two RTTI/vtable identities verified against the actual 1.7.104 executable and matching Address Library. |
| Actual candidate archive audit | Passed: ZIP integrity, SHA-256, actual built DLL, version/SKSE exports, INI, licenses and exact project/dependency sources checked. |
| Linux cross-build | Not verified for 0.2.7. |
| Skyrim / Proton visual testing | Pending for 0.2.7; no automated/local game run or FPS/GPU benchmark is claimed. |

Earlier native launch attempts also produced Code Integrity event 3077. Uninstalling
McAfee did not remove the observed Windows Application Control restriction. No
executable renaming, security-policy change or alternate-host workaround was used.
**The full executable test suite is not green.** A maintainer should run it in an
environment that permits the normal test executables, as documented in README.

The 0.2.7 player archive has exactly four files: DLL, INI, README and licenses.
Its corresponding-source and symbol archives are separate. Current artifact:

```text
Player ZIP: Camera-Colony-0.2.7-297380288db4.zip
ZIP bytes: 588719
ZIP SHA256: bccec71d283f16fa0c1523915795c44e7fb4e0e3e0a896e065fabe43b54a1cb3
DLL SHA256: fdc573f833a6462e9396ffda809894d74271be40a3218a91d85d0d30c4837510
```

The historical v0.1.2 release/tag has not been replaced by this development work.
This source PR does not publish a new binary release or merge the candidate.
See [README.md](README.md) for build, runtime-verification and packaging commands.

## 12. Completion criteria and next-owner checklist

- [ ] Record the exact loaded candidate, Proton/runtime, configuration, weapon,
  skeleton/body/outfit, animation stack and menu-preview behavior for reproduction.
- [ ] Compare 0.2.7 centered vs defective gameplay and same-phase diagnostics in
  the original scene, first with default root placement and then existing tuning.
- [ ] Confirm stable framing during look-down/up, slow/fast yaw and reversals,
  standing and walking/strafe/sprint, without persistent sideways displacement
  or plugin-induced torso penetration. Include near-vertical pitch crossings.
- [ ] Repeat after inventory equip/change/unequip, Magic/Tween/Map transitions,
  paused and supported non-pausing menus, load and repeated 1P/3P switching.
- [ ] Preserve visible, correctly positioned native combat hands/weapons through
  draw/sheath. Cover fists, one/two-handed weapons, bow/crossbow, shield, torch
  and spells; inspect attachment effects and supported body/outfit variants.
- [ ] Confirm disabling/reloading restores native visibility, scales, root pose
  and camera state; unsupported actions fall back without leaving mutations.
- [ ] Resolve or explicitly bound the renderer ownership/cache risks with
  meaningful partial-writer, parent-change and equipment-replacement fixtures.
- [ ] Run Rust/native tests normally on a permitted environment, retain build
  and runtime verification, and record failures separately from launch blocks.
- [ ] Keep the crash investigation separate until causal evidence supports a fix.
- [ ] Update this document and the PR with actual visual results. Do not mark
  the body defect resolved solely because alignment returns valid or read-back
  error is zero. Complete IC/SC parity is a separate longer-term acceptance scope.

The immediate next action is **test the already-built 0.2.7 root-anchor change**
against the documented trigger. If it fails, continue from the measured native
pose and lifecycle evidence above, preserving the weapon fix and avoiding another
unjustified global yaw/scale/camera-offset patch.
