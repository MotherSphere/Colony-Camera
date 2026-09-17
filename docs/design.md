# Colony Camera — approved design

An independent third-person camera mod with original Rust code and a C++
CommonLibSSE-NG bridge. No SmoothCam code, scripts, presets or assets. The working
name is Colony Camera. The initial target is Steam Skyrim 1.7.104.0, SKSE 2.3.1
and the matching Address Library. VR and older runtimes are not promised.

The dependency-free Rust core receives native position, rotation, elapsed time
and a profile. It calculates local offsets and frame-rate-independent exponential
interpolation, bounds lag and resets on transitions, loading and teleports.
The bridge chains the native update, then corrects the candidate position through
native collision before applying it. The accepted position is fed back to the
core to avoid accumulation behind an obstacle. First person, mounts, unsupported
animations, menus and dialogues retain native behavior.

Exploration, combat and aiming profiles support shoulder switching, toggling and
keyboard-triggered configuration reloads. Configuration is validated atomically.
The original design avoids per-frame allocation, file reads and logging. Aiming
retains the native position by default to avoid crosshair misalignment. Custom
crosshairs, projectile paths and an MCM menu are future work; this alpha does not
claim full SmoothCam feature parity.

Verification covers Linux core tests, Windows DLL compilation, export inspection
and hook checks against the actual executable. In-game testing remains required
before a stable release. Authorized fixes may replace the installed DLL after a
backup, with the game closed.

## Integration corrected in 0.1.1

Installation waits for NewGame/PostLoadGame and saves existing entries, including
those owned by other plugins. Prefix guards remain an offline runtime check,
rather than a reason to reject compatible in-memory detours. After Rust filtering
and collision, the bridge updates ThirdPersonState, the root and NiCamera, then
its world-to-screen matrix. Sparse diagnostics cover the first callback, first
result and first displacement per reset.

In 0.1.2, a parent is accepted: the inverse of its world transform determines the
root's local translation. All calculations precede writes, rejecting nonfinite
positions and zero parent scale.
