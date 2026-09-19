//! Independent third-person camera math built from standard easing and exponential
//! response formulas. All inputs and history are values; collision stays in the host.
use crate::{bounded, length, rotate, subtract, Frame};
use serde::{Deserialize, Serialize};
use std::mem::{align_of, size_of};

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Follow {
    pub enabled: u32,
    pub curve: u32,
    /// Response fraction at 60 Hz: zero holds, one snaps.
    pub min_rate: f32,
    pub max_rate: f32,
    pub distance: f32,
}
impl Default for Follow {
    fn default() -> Self {
        Self {
            enabled: 1,
            curve: 0,
            min_rate: 0.25,
            max_rate: 0.66,
            distance: 650.0,
        }
    }
}
impl Follow {
    pub fn valid(&self) -> bool {
        self.enabled <= 1
            && self.curve < 22
            && bounded(self.min_rate, 0.0, 1.0)
            && bounded(self.max_rate, self.min_rate, 1.0)
            && bounded(self.distance, 0.001, 100_000.0)
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AdvancedProfile {
    /// Absolute camera-local side/up, additive camera-local depth.
    pub offset: [f32; 3],
    pub fov: f32,
    pub world: Follow,
    pub local: Follow,
    pub vertical: Follow,
}
impl Default for AdvancedProfile {
    fn default() -> Self {
        Self {
            offset: [0.0; 3],
            fov: 0.0,
            world: Follow::default(),
            local: Follow {
                min_rate: 0.7,
                max_rate: 0.98,
                distance: 60.0,
                ..Follow::default()
            },
            vertical: Follow {
                min_rate: 0.4,
                max_rate: 1.0,
                distance: 55.0,
                ..Follow::default()
            },
        }
    }
}
impl AdvancedProfile {
    pub fn valid(&self) -> bool {
        self.offset.iter().all(|x| bounded(*x, -1000.0, 1000.0))
            && bounded(self.fov, -60.0, 60.0)
            && self.world.valid()
            && self.local.valid()
            && self.vertical.valid()
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ThirdOptions {
    pub enabled: u32,
    pub group_mask: u32,
    #[serde(with = "profiles_serde")]
    pub profiles: [AdvancedProfile; 56],
    pub offset_curve: u32,
    pub zoom_curve: u32,
    pub fov_curve: u32,
    pub offset_duration: f32,
    pub zoom_duration: f32,
    pub fov_duration: f32,
    pub clamp_mask: u32,
    pub clamp_min: [f32; 3],
    pub clamp_max: [f32; 3],
    pub pitch_enabled: u32,
    pub pitch_after: u32,
    pub pitch_curve: u32,
    pub pitch_max_angle: f32,
    pub pitch_zoom: f32,
    pub mirror_clamp: u32,
}
// Serde implements fixed arrays only through length 32. The sequence conversion
// is confined to configuration I/O; the per-frame ABI never allocates.
mod profiles_serde {
    use super::AdvancedProfile;
    use serde::{Deserialize, Deserializer, Serialize, Serializer};
    pub fn serialize<S: Serializer>(
        profiles: &[AdvancedProfile; 56],
        s: S,
    ) -> Result<S::Ok, S::Error> {
        profiles.as_slice().serialize(s)
    }
    pub fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<[AdvancedProfile; 56], D::Error> {
        Vec::<AdvancedProfile>::deserialize(d)?
            .try_into()
            .map_err(|_| serde::de::Error::custom("expected exactly 56 advanced profiles"))
    }
}
impl Default for ThirdOptions {
    fn default() -> Self {
        Self {
            enabled: 0,
            group_mask: 127,
            profiles: [AdvancedProfile::default(); 56],
            offset_curve: 0,
            zoom_curve: 0,
            fov_curve: 0,
            offset_duration: 0.3,
            zoom_duration: 0.3,
            fov_duration: 0.3,
            clamp_mask: 0,
            clamp_min: [-300.0; 3],
            clamp_max: [300.0; 3],
            pitch_enabled: 0,
            pitch_after: 0,
            pitch_curve: 0,
            pitch_max_angle: 90.0,
            pitch_zoom: 0.0,
            mirror_clamp: 0,
        }
    }
}
impl ThirdOptions {
    pub fn valid(&self) -> bool {
        self.globals_valid() && self.profiles.iter().all(AdvancedProfile::valid)
    }
    fn globals_valid(&self) -> bool {
        [
            self.enabled,
            self.pitch_enabled,
            self.pitch_after,
            self.mirror_clamp,
        ]
        .iter()
        .all(|x| *x <= 1)
            && self.group_mask & !0x3fff == 0
            && self.clamp_mask & !7 == 0
            && [
                self.offset_curve,
                self.zoom_curve,
                self.fov_curve,
                self.pitch_curve,
            ]
            .iter()
            .all(|x| *x < 22)
            && [self.offset_duration, self.zoom_duration, self.fov_duration]
                .iter()
                .all(|x| bounded(*x, 0.0, 10.0))
            && (0..3).all(|i| {
                bounded(self.clamp_min[i], -10_000.0, 0.0)
                    && bounded(self.clamp_max[i], 0.0, 10_000.0)
            })
            && bounded(self.pitch_max_angle, 0.001, 90.0)
            && bounded(self.pitch_zoom, -1000.0, 1000.0)
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AdvancedFrame {
    pub native: Frame,
    pub focus: [f32; 3],
    pub group: u32,
    pub stance: u32,
    /// Positive downward pitch. Looking upward does not activate pitch zoom.
    pub pitch_degrees: f32,
    /// Actual interpolated native camera-local side/up, not the target INI values.
    pub native_side: f32,
    pub native_up: f32,
    pub shoulder_mirrored: u32,
}
impl AdvancedFrame {
    fn valid(&self) -> bool {
        self.native
            .position
            .iter()
            .chain(&self.focus)
            .all(|x| bounded(*x, -1e8, 1e8))
            && self.native.rotation.iter().all(|x| bounded(*x, -2.0, 2.0))
            && self.native.rotation.iter().map(|x| x * x).sum::<f32>() >= 0.01
            && self.native.dt.is_finite()
            && self.native.dt >= 0.0
            && bounded(self.native.world_fov, 1.0, 179.0)
            && self.group < 14
            && self.stance < 4
            && self.shoulder_mirrored <= 1
            && bounded(self.pitch_degrees, -90.0, 90.0)
            && bounded(self.native_side, -10_000.0, 10_000.0)
            && bounded(self.native_up, -10_000.0, 10_000.0)
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct AdvancedState {
    pub position: [f32; 3],
    pub native: [f32; 3],
    pub initialized: u32,
    pub anchor: [f32; 3],
    pub focus: [f32; 3],
    pub orbit: [f32; 3],
    pub offset: [f32; 3],
    pub offset_from: [f32; 3],
    pub offset_target: [f32; 3],
    pub fov: f32,
    pub fov_delta: f32,
    pub fov_from: f32,
    pub fov_target: f32,
    /// Elapsed seconds: side/up, depth, FOV.
    pub elapsed: [f32; 3],
}
impl AdvancedState {
    fn valid(&self) -> bool {
        self.initialized == 1
            && self
                .position
                .iter()
                .chain(&self.native)
                .chain(&self.anchor)
                .chain(&self.focus)
                .chain(&self.orbit)
                .all(|x| bounded(*x, -2.1e8, 2.1e8))
            && self
                .offset
                .iter()
                .chain(&self.offset_from)
                .chain(&self.offset_target)
                .all(|x| bounded(*x, -1000.0, 1000.0))
            && [self.fov_delta, self.fov_from, self.fov_target]
                .iter()
                .all(|x| bounded(*x, -60.0, 60.0))
            && bounded(self.fov, 1.0, 179.0)
            && self.elapsed.iter().all(|x| bounded(*x, 0.0, 10.0))
    }
}
/// Standard monotonic easing functions. IDs match the public preset enum; no
/// implementation from another camera project is used. Invalid inputs are zero.
pub fn advanced_curve(curve: u32, t: f32) -> f32 {
    if curve >= 22 || !t.is_finite() {
        return 0.0;
    }
    let t = t.clamp(0.0, 1.0);
    if t == 0.0 || t == 1.0 || curve == 0 {
        return t;
    }
    let family = (curve - 1) / 3;
    let mode = (curve - 1) % 3;
    let ease_in = |x: f32| match family {
        0..=3 => x.powi(family as i32 + 2),
        4 => 1.0 - (x * std::f32::consts::FRAC_PI_2).cos(),
        5 => 1.0 - (1.0 - x * x).max(0.0).sqrt(),
        _ => 2.0_f32.powf(10.0 * x - 10.0),
    };
    match mode {
        0 => ease_in(t),
        1 => 1.0 - ease_in(1.0 - t),
        _ if t < 0.5 => 0.5 * ease_in(2.0 * t),
        _ => 1.0 - 0.5 * ease_in(2.0 * (1.0 - t)),
    }
    .clamp(0.0, 1.0)
}
fn mix(a: f32, b: f32, fraction: f32) -> f32 {
    if fraction == 0.0 {
        return a;
    }
    if fraction == 1.0 {
        return b;
    }
    (a + (b - a) * fraction).clamp(a.min(b), a.max(b))
}
fn response(follow: Follow, distance: f32, dt: f32) -> f32 {
    if follow.enabled == 0 {
        return 1.0;
    }
    if dt == 0.0 {
        return 0.0;
    }
    let rate = mix(
        follow.min_rate,
        follow.max_rate,
        advanced_curve(follow.curve, distance / follow.distance),
    );
    if rate == 1.0 {
        return 1.0;
    }
    // Convert the reference-frame response to elapsed time without cancellation.
    -((dt * 60.0) * (-rate).ln_1p()).exp_m1()
}
fn follow_anchor(mut current: [f32; 3], target: [f32; 3], p: AdvancedProfile, dt: f32) -> [f32; 3] {
    // Bounded substeps limit distance-dependent response error at low frame rates.
    // Constant-rate response remains an exact exponential at any timestep.
    let count = (dt * 240.0).ceil().max(1.0) as u32;
    for _ in 0..count {
        let delta = subtract(target, current);
        let xy = response(p.world, delta[0].hypot(delta[1]), dt / count as f32);
        let z = response(p.vertical, delta[2].abs(), dt / count as f32);
        current = [
            mix(current[0], target[0], xy),
            mix(current[1], target[1], xy),
            mix(current[2], target[2], z),
        ];
    }
    current
}
fn follow_orbit(mut current: [f32; 3], target: [f32; 3], follow: Follow, dt: f32) -> [f32; 3] {
    let count = (dt * 240.0).ceil().max(1.0) as u32;
    for _ in 0..count {
        let alpha = response(follow, length(subtract(target, current)), dt / count as f32);
        current = std::array::from_fn(|i| mix(current[i], target[i], alpha));
    }
    current
}
fn progress(elapsed: &mut f32, duration: f32, dt: f32, curve: u32) -> f32 {
    *elapsed = (*elapsed + dt).min(duration);
    // Absorb float accumulation at the requested endpoint, not an asymptotic tail.
    if duration == 0.0 || duration - *elapsed <= 128.0 * f32::EPSILON * duration {
        *elapsed = duration;
        1.0
    } else {
        advanced_curve(curve, *elapsed / duration)
    }
}
/// Validated pure value entry point. initialized=0 delegates to the old engine.
/// Only global controls and the selected profile are consumed and validated here;
/// validate the entire configuration before Apply. Reset history when applying it.
pub fn step_advanced(
    state: AdvancedState,
    frame: AdvancedFrame,
    options: &ThirdOptions,
) -> AdvancedState {
    if !frame.valid()
        || !options.globals_valid()
        || !options.profiles[(frame.group * 4 + frame.stance) as usize].valid()
    {
        return AdvancedState::default();
    }
    step_validated(state, frame, options)
}
fn step_validated(
    mut state: AdvancedState,
    frame: AdvancedFrame,
    o: &ThirdOptions,
) -> AdvancedState {
    if o.enabled == 0 || o.group_mask & (1 << frame.group) == 0 {
        return AdvancedState {
            native: frame.native.position,
            position: frame.native.position,
            fov: frame.native.world_fov,
            ..AdvancedState::default()
        };
    }
    let p = o.profiles[(frame.group * 4 + frame.stance) as usize];
    let q = frame.native.rotation;
    let pitch = if o.pitch_enabled == 1 {
        o.pitch_zoom * advanced_curve(o.pitch_curve, frame.pitch_degrees / o.pitch_max_angle)
    } else {
        0.0
    };
    let neutral_correction = rotate(
        q,
        [
            -frame.native_side,
            if o.pitch_after == 0 { pitch } else { 0.0 },
            -frame.native_up,
        ],
    );
    let orbit_target =
        std::array::from_fn(|i| frame.native.position[i] - frame.focus[i] + neutral_correction[i]);
    let mut target = p.offset;
    if frame.shoulder_mirrored == 1 {
        target[0] = -target[0];
    }
    let fov_target = if p.fov == 0.0 {
        0.0
    } else {
        ((frame.native.world_fov + p.fov).clamp(30.0, 150.0) - frame.native.world_fov)
            .clamp(-60.0, 60.0)
    };
    let reset = !state.valid()
        || frame.native.reset != 0
        || frame.native.dt > 0.25
        || length(subtract(frame.focus, state.focus)) > 1000.0
        || length(subtract(frame.native.position, state.native)) > 1000.0;
    if reset {
        state.anchor = frame.focus;
        state.orbit = orbit_target;
        state.offset = target;
        state.offset_from = target;
        state.offset_target = target;
        state.fov_delta = fov_target;
        state.fov_from = fov_target;
        state.fov_target = fov_target;
        state.elapsed = [o.offset_duration, o.zoom_duration, o.fov_duration];
    } else {
        if target[0] != state.offset_target[0] || target[2] != state.offset_target[2] {
            for i in [0, 2] {
                state.offset_from[i] = state.offset[i];
                state.offset_target[i] = target[i];
            }
            state.elapsed[0] = 0.0;
        }
        if target[1] != state.offset_target[1] {
            state.offset_from[1] = state.offset[1];
            state.offset_target[1] = target[1];
            state.elapsed[1] = 0.0;
        }
        if fov_target != state.fov_target {
            state.fov_from = state.fov_delta;
            state.fov_target = fov_target;
            state.elapsed[2] = 0.0;
        }
        let dt = frame.native.dt;
        let xy = progress(&mut state.elapsed[0], o.offset_duration, dt, o.offset_curve);
        let depth = progress(&mut state.elapsed[1], o.zoom_duration, dt, o.zoom_curve);
        let fov = progress(&mut state.elapsed[2], o.fov_duration, dt, o.fov_curve);
        state.offset = std::array::from_fn(|i| {
            mix(
                state.offset_from[i],
                state.offset_target[i],
                if i == 1 { depth } else { xy },
            )
        });
        state.fov_delta = mix(state.fov_from, state.fov_target, fov);
        state.anchor = follow_anchor(state.anchor, frame.focus, p, dt);
        state.orbit = follow_orbit(state.orbit, orbit_target, p.local, dt);
    }
    let mut offset = state.offset;
    if o.pitch_after == 1 {
        offset[1] += pitch;
    }
    let offset = rotate(q, offset);
    let desired = std::array::from_fn(|i| frame.focus[i] + orbit_target[i] + offset[i]);
    state.position = std::array::from_fn(|i| state.anchor[i] + state.orbit[i] + offset[i]);
    let lag = subtract(state.position, desired);
    let local_lag = rotate([q[0], -q[1], -q[2], -q[3]], lag);
    let mut limited = local_lag;
    for i in 0..3 {
        if o.clamp_mask & (1 << i) != 0 {
            let (min, max) = if i == 0 && o.mirror_clamp == 1 && frame.shoulder_mirrored == 1 {
                (-o.clamp_max[i], -o.clamp_min[i])
            } else {
                (o.clamp_min[i], o.clamp_max[i])
            };
            limited[i] = local_lag[i].clamp(min, max);
        }
    }
    if limited != local_lag {
        let corrected = rotate(q, limited);
        for i in 0..3 {
            // Constrain the relative camera, preserving the player-follow anchor.
            let correction = corrected[i] - lag[i];
            state.orbit[i] += correction;
            state.position[i] = desired[i] + corrected[i];
        }
    }
    state.native = frame.native.position;
    state.focus = frame.focus;
    state.fov = (frame.native.world_fov + state.fov_delta).clamp(1.0, 179.0);
    state.initialized = 1;
    state
}
fn region<T>(p: *const T) -> Option<(usize, usize)> {
    let start = p as usize;
    if p.is_null() || start & (align_of::<T>() - 1) != 0 {
        return None;
    }
    Some((start, start.checked_add(size_of::<T>())?))
}
fn overlap(a: (usize, usize), b: (usize, usize)) -> bool {
    a.0 < b.1 && b.0 < a.1
}
/// # Safety
/// Output must reference live writable storage for one ThirdOptions.
#[no_mangle]
pub unsafe extern "C" fn cc_advanced_defaults(output: *mut ThirdOptions) -> u32 {
    if region(output).is_none() {
        return 1;
    }
    unsafe {
        output.write(ThirdOptions::default());
    }
    0
}
/// # Safety
/// Options must reference live readable storage for one ThirdOptions.
#[no_mangle]
pub unsafe extern "C" fn cc_advanced_validate(options: *const ThirdOptions) -> u32 {
    if region(options).is_none() {
        return 1;
    }
    if unsafe { &*options }.valid() {
        0
    } else {
        3
    }
}
/// Pointer value-only ABI. On failure output is unchanged. Disabled/masked groups
/// succeed with initialized=0, requesting legacy fallback rather than camera ownership.
/// # Safety
/// Inputs must be readable, output writable, for their complete declared types.
/// Regions must be disjoint and remain live for the call. No pointer is retained.
#[no_mangle]
pub unsafe extern "C" fn cc_step_advanced(
    state: *const AdvancedState,
    frame: *const AdvancedFrame,
    options: *const ThirdOptions,
    output: *mut AdvancedState,
) -> u32 {
    let (Some(s), Some(f), Some(o), Some(out)) = (
        region(state),
        region(frame),
        region(options),
        region(output),
    ) else {
        return 1;
    };
    let regions = [s, f, o, out];
    for i in 0..4 {
        for j in i + 1..4 {
            if overlap(regions[i], regions[j]) {
                return 1;
            }
        }
    }
    std::panic::catch_unwind(|| {
        let frame = unsafe { frame.read() };
        let options = unsafe { &*options };
        if !frame.valid()
            || !options.globals_valid()
            || !options.profiles[(frame.group * 4 + frame.stance) as usize].valid()
        {
            return 3;
        }
        let next = step_validated(unsafe { state.read() }, frame, options);
        unsafe {
            output.write(next);
        }
        0
    })
    .unwrap_or(5)
}
