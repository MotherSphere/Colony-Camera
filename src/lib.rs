//! Independent deterministic camera math. Positions are world-space; rotations are w,x,y,z.
mod config;
mod coordinator;
pub use config::*;
pub use coordinator::*;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct State {
    pub position: [f32; 3],
    pub native: [f32; 3],
    pub initialized: u32,
    pub base: [f32; 3],
    pub offset: [f32; 3],
    pub zoom: f32,
    pub fov: f32,
    pub fov_delta: f32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Frame {
    pub position: [f32; 3],
    pub rotation: [f32; 4],
    pub dt: f32,
    pub reset: u32,
    pub world_fov: f32,
}
impl Default for Frame {
    fn default() -> Self {
        Self {
            position: [0.0; 3],
            rotation: [1.0, 0.0, 0.0, 0.0],
            dt: 0.0,
            reset: 0,
            world_fov: 75.0,
        }
    }
}
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Profile {
    pub offset: [f32; 3],
    pub half_life: f32,
    pub max_lag: f32,
    pub offset_half_life: f32,
    pub zoom_half_life: f32,
    pub fov_half_life: f32,
    pub zoom: f32,
    pub fov_offset: f32,
}
impl Default for Profile {
    fn default() -> Self {
        Self {
            offset: [0.0; 3],
            half_life: 0.08,
            max_lag: 60.0,
            offset_half_life: 0.08,
            zoom_half_life: 0.12,
            fov_half_life: 0.12,
            zoom: 0.0,
            fov_offset: 0.0,
        }
    }
}
fn length(v: [f32; 3]) -> f32 {
    v[0].hypot(v[1]).hypot(v[2])
}
fn subtract(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    std::array::from_fn(|i| a[i] - b[i])
}
fn rotate(q: [f32; 4], v: [f32; 3]) -> [f32; 3] {
    let norm = q.iter().map(|x| x * x).sum::<f32>().sqrt();
    let [w, x, y, z] = q.map(|x| x / norm);
    let t = [
        2.0 * (y * v[2] - z * v[1]),
        2.0 * (z * v[0] - x * v[2]),
        2.0 * (x * v[1] - y * v[0]),
    ];
    [
        v[0] + w * t[0] + y * t[2] - z * t[1],
        v[1] + w * t[1] + z * t[0] - x * t[2],
        v[2] + w * t[2] + x * t[1] - y * t[0],
    ]
}
fn bounded(value: f32, min: f32, max: f32) -> bool {
    value.is_finite() && (min..=max).contains(&value)
}
impl Profile {
    pub fn valid(self) -> bool {
        self.offset.iter().all(|x| bounded(*x, -300.0, 300.0))
            && [
                self.half_life,
                self.offset_half_life,
                self.zoom_half_life,
                self.fov_half_life,
            ]
            .iter()
            .all(|x| bounded(*x, 0.0, 1.0))
            && bounded(self.max_lag, 0.0, 300.0)
            && bounded(self.zoom, -300.0, 300.0)
            && bounded(self.fov_offset, -60.0, 60.0)
    }
}
fn interpolate(current: f32, target: f32, dt: f32, half_life: f32) -> f32 {
    if half_life == 0.0 {
        return target;
    }
    let alpha = -(-std::f32::consts::LN_2 * dt / half_life).exp_m1();
    // Rounding near alpha=1 can otherwise overshoot a valid endpoint and make
    // the next frame reject its own history as out of range.
    (current + (target - current) * alpha).clamp(current.min(target), current.max(target))
}
pub fn step(mut state: State, frame: Frame, profile: Profile) -> State {
    // Invalid engine input must never be published to the camera graph.
    if !frame.position.iter().all(|x| bounded(*x, -1e8, 1e8))
        || !frame.rotation.iter().all(|x| bounded(*x, -2.0, 2.0))
        || frame.rotation.iter().map(|x| x * x).sum::<f32>() < 0.01
        || !frame.dt.is_finite()
        || frame.dt < 0.0
        || !bounded(frame.world_fov, 1.0, 179.0)
        || !profile.valid()
    {
        state.initialized = 0;
        return state;
    }
    let reset = state.initialized != 1
        || frame.reset != 0
        || frame.dt > 0.25
        || !state
            .position
            .iter()
            .chain(&state.native)
            .chain(&state.base)
            .all(|x| bounded(*x, -1.1e8, 1.1e8))
        || !state.offset.iter().all(|x| bounded(*x, -300.0, 300.0))
        || !bounded(state.zoom, -300.0, 300.0)
        || !bounded(state.fov, 1.0, 179.0)
        || !bounded(state.fov_delta, -60.0, 60.0)
        || length(subtract(frame.position, state.native)) > 1000.0;
    // Bound the adjusted target, then interpolate its effective delta. Clamping
    // every intermediate result to 30..150 would prevent a smooth return to an
    // unadjusted native FOV outside that range.
    let target_fov_delta = if profile.fov_offset == 0.0 {
        0.0
    } else {
        // Subtraction after clamping can round slightly outside +/-60 even
        // though both the requested offset and target are valid.
        ((frame.world_fov + profile.fov_offset).clamp(30.0, 150.0) - frame.world_fov)
            .clamp(-60.0, 60.0)
    };
    if reset {
        state.base = frame.position;
        state.offset = profile.offset;
        state.zoom = profile.zoom;
        state.fov_delta = target_fov_delta;
    } else {
        state.base = std::array::from_fn(|i| {
            interpolate(
                state.base[i],
                frame.position[i],
                frame.dt,
                profile.half_life,
            )
        });
        state.offset = std::array::from_fn(|i| {
            interpolate(
                state.offset[i],
                profile.offset[i],
                frame.dt,
                profile.offset_half_life,
            )
        });
        state.zoom = interpolate(state.zoom, profile.zoom, frame.dt, profile.zoom_half_life);
        state.fov_delta = interpolate(
            state.fov_delta,
            target_fov_delta,
            frame.dt,
            profile.fov_half_life,
        );
    }
    // Interpolate only our additive FOV adjustment. Engine-driven FOV/zoom
    // changes stay immediate and never become part of the plugin's history.
    state.fov = (frame.world_fov + state.fov_delta).clamp(1.0, 179.0);
    let lag = subtract(state.base, frame.position);
    let distance = length(lag);
    if distance > profile.max_lag {
        state.base =
            std::array::from_fn(|i| frame.position[i] + lag[i] * (profile.max_lag / distance));
    }
    let mut local_offset = state.offset;
    local_offset[1] += state.zoom;
    let world_offset = rotate(frame.rotation, local_offset);
    state.position = std::array::from_fn(|i| state.base[i] + world_offset[i]);
    state.native = frame.position;
    state.initialized = 1;
    state
}
/// Value-only ABI: no game pointers or per-frame allocation cross the boundary.
#[no_mangle]
pub extern "C" fn cc_step(state: State, frame: Frame, profile: Profile) -> State {
    step(state, frame, profile)
}
