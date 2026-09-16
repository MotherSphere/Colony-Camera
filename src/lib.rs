//! Independent camera math. Positions are world-space; rotations are w,x,y,z.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct State {
    pub position: [f32; 3],
    pub native: [f32; 3],
    pub initialized: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Frame {
    pub position: [f32; 3],
    pub rotation: [f32; 4],
    pub dt: f32,
    pub reset: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Profile {
    pub offset: [f32; 3],
    pub half_life: f32,
    pub max_lag: f32,
}
impl Default for Profile {
    fn default() -> Self {
        Self {
            offset: [0.0; 3],
            half_life: 0.08,
            max_lag: 60.0,
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
impl Profile {
    pub fn valid(self) -> bool {
        self.offset
            .iter()
            .all(|x| x.is_finite() && x.abs() <= 300.0)
            && self.half_life.is_finite()
            && (0.0..=1.0).contains(&self.half_life)
            && self.max_lag.is_finite()
            && (0.0..=300.0).contains(&self.max_lag)
    }
}
pub fn step(mut state: State, frame: Frame, profile: Profile) -> State {
    // Invalid engine input must never propagate NaN to the camera graph.
    if !frame
        .position
        .iter()
        .all(|x| x.is_finite() && x.abs() < 1e8)
        || !frame
            .rotation
            .iter()
            .all(|x| x.is_finite() && x.abs() <= 2.0)
        || frame.rotation.iter().map(|x| x * x).sum::<f32>() < 0.01
        || !frame.dt.is_finite()
        || frame.dt < 0.0
        || !profile.valid()
    {
        state.initialized = 0;
        return state;
    }
    let offset = rotate(frame.rotation, profile.offset);
    let target = std::array::from_fn(|i| frame.position[i] + offset[i]);
    let reset = state.initialized != 1
        || frame.reset != 0
        || frame.dt > 0.25
        || !state.position.iter().all(|x| x.is_finite())
        || !state.native.iter().all(|x| x.is_finite())
        || length(subtract(frame.position, state.native)) > 1000.0;
    if reset || profile.half_life == 0.0 {
        state.position = target;
    } else {
        let alpha = -(-std::f32::consts::LN_2 * frame.dt / profile.half_life).exp_m1();
        state.position =
            std::array::from_fn(|i| state.position[i] + (target[i] - state.position[i]) * alpha);
        let lag = subtract(state.position, target);
        let distance = length(lag);
        if distance > profile.max_lag {
            state.position =
                std::array::from_fn(|i| target[i] + lag[i] * (profile.max_lag / distance));
        }
    }
    state.native = frame.position;
    state.initialized = 1;
    state
}
/// Value-only ABI: no game pointers or allocation cross the language boundary.
#[no_mangle]
pub extern "C" fn cc_step(state: State, frame: Frame, profile: Profile) -> State {
    step(state, frame, profile)
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Config {
    pub profiles: [Profile; 3],
    pub keys: [u32; 3],
    pub enabled: u32,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            profiles: [
                Profile::default(),
                Profile {
                    half_life: 0.04,
                    ..Profile::default()
                },
                Profile {
                    half_life: 0.0,
                    max_lag: 0.0,
                    ..Profile::default()
                },
            ],
            keys: [0x42, 0x43, 0x44], // Ctrl+F8 / Ctrl+F9 / Ctrl+F10, SKSE keyboard scan codes
            enabled: 1,
        }
    }
}
/// Parse atomically: a bad setting rejects the whole replacement configuration.
pub fn parse_config(text: &str) -> Result<Config, String> {
    let mut config = Config::default();
    let mut section = "general";
    let mut seen = std::collections::HashSet::new();
    for (index, raw) in text.lines().enumerate() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with(['#', ';']) {
            continue;
        }
        if let Some(name) = line.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
            if !["general", "exploration", "combat", "aim"].contains(&name) {
                return Err(format!("line {}: unknown section", index + 1));
            }
            section = name;
            continue;
        }
        let (key, value) = line
            .split_once('=')
            .ok_or_else(|| format!("line {}: expected key=value", index + 1))?;
        let (key, value) = (key.trim(), value.trim());
        if !seen.insert((section, key)) {
            return Err(format!("line {}: duplicate key", index + 1));
        }
        if section == "general" {
            if key == "enabled" {
                config.enabled = match value {
                    "true" => 1,
                    "false" => 0,
                    _ => return Err("enabled must be true or false".into()),
                };
            } else {
                let idx = match key {
                    "toggle_key" => 0,
                    "shoulder_key" => 1,
                    "reload_key" => 2,
                    _ => return Err(format!("unknown key: {key}")),
                };
                let code: u32 = value.parse().map_err(|_| "key must be a scan code")?;
                if !(1..=211).contains(&code) || [29, 157].contains(&code) {
                    return Err(
                        "key outside keyboard scan code range or reserved Ctrl modifier".into(),
                    );
                }
                config.keys[idx] = code;
            }
        } else {
            let idx = match section {
                "exploration" => 0,
                "combat" => 1,
                _ => 2,
            };
            let p = &mut config.profiles[idx];
            let number: f32 = value.parse().map_err(|_| "expected number")?;
            match key {
                "x" => p.offset[0] = number,
                "y" => p.offset[1] = number,
                "z" => p.offset[2] = number,
                "half_life" => p.half_life = number,
                "max_lag" => p.max_lag = number,
                _ => return Err(format!("unknown key: {key}")),
            }
            if !p.valid() {
                return Err(format!("invalid value for {key}"));
            }
        }
    }
    if config.keys[0] == config.keys[1]
        || config.keys[0] == config.keys[2]
        || config.keys[1] == config.keys[2]
    {
        return Err("keyboard bindings must differ".into());
    }
    Ok(config)
}
#[no_mangle]
pub extern "C" fn cc_defaults() -> Config {
    Config::default()
}
/// # Safety
/// `bytes` must reference `len` readable bytes; `output` must be writable and aligned.
/// Neither region may overlap. Returns nonzero without writing on any parse error.
#[no_mangle]
pub unsafe extern "C" fn cc_parse_config(bytes: *const u8, len: usize, output: *mut Config) -> u32 {
    if bytes.is_null() || output.is_null() || len > 65536 {
        return 1;
    }
    let data = unsafe { std::slice::from_raw_parts(bytes, len) };
    let Ok(text) = std::str::from_utf8(data) else {
        return 2;
    };
    match parse_config(text) {
        Ok(config) => {
            unsafe {
                output.write(config);
            }
            0
        }
        Err(_) => 3,
    }
}
