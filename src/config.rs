//! Atomic configuration parsing, validation and canonical own-format persistence.
use crate::{Profile, AIM, AIRBORNE, COMBAT, LOCOMOTION_MASK, PROFILE_COUNT, SNEAK, SPRINT, SWIM};
use std::fmt::Write;
use std::mem::{align_of, size_of};

pub const MAX_CONFIG_BYTES: usize = 65536;
const SECTIONS: [&str; PROFILE_COUNT] = [
    "exploration",
    "combat",
    "aim",
    "sprint",
    "sneak",
    "swim",
    "airborne",
];
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Config {
    pub profiles: [Profile; PROFILE_COUNT],
    pub keys: [u32; 3],
    pub enabled: u32,
    pub menu_key: u32,
    pub first_person_enabled: u32,
    pub locomotion_profiles: u32,
}
impl Default for Config {
    fn default() -> Self {
        let mut profiles = [Profile::default(); PROFILE_COUNT];
        profiles[COMBAT as usize].half_life = 0.04;
        profiles[COMBAT as usize].offset_half_life = 0.04;
        profiles[AIM as usize] = Profile {
            half_life: 0.0,
            max_lag: 0.0,
            offset_half_life: 0.0,
            zoom_half_life: 0.0,
            fov_half_life: 0.0,
            ..Profile::default()
        };
        profiles[SPRINT as usize].half_life = 0.06;
        profiles[SNEAK as usize].half_life = 0.10;
        profiles[SWIM as usize].half_life = 0.12;
        profiles[AIRBORNE as usize].half_life = 0.04;
        Self {
            profiles,
            keys: [0x42, 0x43, 0x44],
            enabled: 1,
            menu_key: 0x41,
            first_person_enabled: 0,
            locomotion_profiles: 0,
        }
    }
}
fn key_valid(key: u32) -> bool {
    (1..=211).contains(&key) && key != 29 && key != 157
}
impl Config {
    pub fn valid(&self) -> bool {
        let keys = [self.keys[0], self.keys[1], self.keys[2], self.menu_key];
        self.enabled <= 1
            && self.first_person_enabled <= 1
            && self.locomotion_profiles & !LOCOMOTION_MASK == 0
            && self.profiles.iter().all(|profile| profile.valid())
            && keys.iter().all(|key| key_valid(*key))
            && keys
                .iter()
                .enumerate()
                .all(|(index, key)| !keys[..index].contains(key))
    }
}
fn boolean(value: &str) -> Result<u32, String> {
    match value {
        "true" => Ok(1),
        "false" => Ok(0),
        _ => Err("expected true or false".into()),
    }
}
/// Missing legacy fields retain their old meaning. New locomotion sections opt
/// in when present, unless `active=false`; legacy files never activate them.
pub fn parse_config(text: &str) -> Result<Config, String> {
    if text.len() > MAX_CONFIG_BYTES {
        return Err("configuration exceeds 65536 bytes".into());
    }
    let mut config = Config::default();
    let mut section = "general";
    let mut seen = std::collections::HashSet::new();
    for (index, raw) in text.trim_start_matches('\u{feff}').lines().enumerate() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with(['#', ';']) {
            continue;
        }
        if let Some(name) = line.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
            if name != "general" && name != "first_person" && !SECTIONS.contains(&name) {
                return Err(format!("line {}: unknown section", index + 1));
            }
            section = name;
            if let Some(i) = SECTIONS.iter().position(|s| *s == name) {
                if i >= SPRINT as usize && !seen.contains(&(section, "active")) {
                    config.locomotion_profiles |= 1 << i;
                }
            }
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
            match key {
                "format_version" => {
                    if value != "1" && value != "2" {
                        return Err("unsupported format_version".into());
                    }
                }
                "enabled" => config.enabled = boolean(value)?,
                "toggle_key" | "shoulder_key" | "reload_key" | "menu_key" => {
                    let code: u32 = value.parse().map_err(|_| "key must be a scan code")?;
                    if !key_valid(code) {
                        return Err("key outside keyboard range or reserved Ctrl modifier".into());
                    }
                    match key {
                        "toggle_key" => config.keys[0] = code,
                        "shoulder_key" => config.keys[1] = code,
                        "reload_key" => config.keys[2] = code,
                        _ => config.menu_key = code,
                    }
                }
                _ => return Err(format!("unknown key: {key}")),
            }
        } else if section == "first_person" {
            if key != "enabled" {
                return Err(format!("unknown first_person key: {key}"));
            }
            config.first_person_enabled = boolean(value)?;
        } else {
            let i = SECTIONS
                .iter()
                .position(|s| *s == section)
                .ok_or("invalid section")?;
            if key == "active" && i >= SPRINT as usize {
                if boolean(value)? != 0 {
                    config.locomotion_profiles |= 1 << i;
                } else {
                    config.locomotion_profiles &= !(1 << i);
                }
                continue;
            }
            let p = &mut config.profiles[i];
            let number: f32 = value.parse().map_err(|_| "expected number")?;
            match key {
                "x" => p.offset[0] = number,
                "y" => p.offset[1] = number,
                "z" => p.offset[2] = number,
                "half_life" => p.half_life = number,
                "max_lag" => p.max_lag = number,
                "offset_half_life" => p.offset_half_life = number,
                "zoom_half_life" => p.zoom_half_life = number,
                "fov_half_life" => p.fov_half_life = number,
                "zoom" => p.zoom = number,
                "fov_offset" => p.fov_offset = number,
                _ => return Err(format!("unknown key: {key}")),
            }
            if !p.valid() {
                return Err(format!("invalid value for {key}"));
            }
        }
    }
    for (i, name) in SECTIONS.iter().enumerate() {
        if seen.contains(&(*name, "half_life")) && !seen.contains(&(*name, "offset_half_life")) {
            config.profiles[i].offset_half_life = config.profiles[i].half_life;
        }
    }
    // A historical remap to F7 must remain valid when adding the new menu key.
    if !seen.contains(&("general", "menu_key")) && config.keys.contains(&config.menu_key) {
        config.menu_key = (59..=211)
            .find(|key| key_valid(*key) && !config.keys.contains(key))
            .ok_or("no menu key available")?;
    }
    if !config.valid() {
        return Err("invalid configuration or overlapping keyboard bindings".into());
    }
    Ok(config)
}
/// Canonical, versioned Camera Colony INI. Float formatting round-trips exactly.
/// Persistence and atomic file replacement belong to the native bridge.
pub fn serialize_config(config: &Config) -> Result<String, String> {
    if !config.valid() {
        return Err("invalid configuration".into());
    }
    let mut text = String::with_capacity(2048);
    writeln!(text, "[general]\nformat_version=2\nenabled={}\ntoggle_key={}\nshoulder_key={}\nreload_key={}\nmenu_key={}\n\n[first_person]\nenabled={}\n",
        config.enabled != 0, config.keys[0], config.keys[1], config.keys[2], config.menu_key, config.first_person_enabled != 0).map_err(|_| "format failed")?;
    for (i, name) in SECTIONS.iter().enumerate() {
        let p = config.profiles[i];
        writeln!(text, "[{name}]").map_err(|_| "format failed")?;
        if i >= SPRINT as usize {
            writeln!(
                text,
                "active={}",
                config.locomotion_profiles & (1 << i) != 0
            )
            .map_err(|_| "format failed")?;
        }
        writeln!(text, "x={}\ny={}\nz={}\nhalf_life={}\nmax_lag={}\noffset_half_life={}\nzoom_half_life={}\nfov_half_life={}\nzoom={}\nfov_offset={}\n",
            p.offset[0], p.offset[1], p.offset[2], p.half_life, p.max_lag, p.offset_half_life,
            p.zoom_half_life, p.fov_half_life, p.zoom, p.fov_offset).map_err(|_| "format failed")?;
    }
    Ok(text)
}
#[no_mangle]
pub extern "C" fn cc_defaults() -> Config {
    Config::default()
}
fn aligned<T>(pointer: *const T) -> bool {
    !pointer.is_null() && (pointer as usize & (align_of::<T>() - 1)) == 0
}
fn region(start: usize, size: usize) -> Option<(usize, usize)> {
    start.checked_add(size).map(|end| (start, end))
}
fn overlap(a: (usize, usize), b: (usize, usize)) -> bool {
    a.0 < b.1 && b.0 < a.1
}
fn guarded(action: impl FnOnce() -> u32 + std::panic::UnwindSafe) -> u32 {
    std::panic::catch_unwind(action).unwrap_or(5)
}
/// # Safety
/// `config` must reference one live, readable Config for the call. Alignment and
/// null are checked; pointer provenance/readability remain the caller's duty.
#[no_mangle]
pub unsafe extern "C" fn cc_validate_config(config: *const Config) -> u32 {
    if !aligned(config) || region(config as usize, size_of::<Config>()).is_none() {
        return 1;
    }
    guarded(|| {
        if unsafe { config.read() }.valid() {
            0
        } else {
            3
        }
    })
}
/// # Safety
/// `bytes` must reference `len` readable bytes; `output` must reference one live,
/// writable Config. Regions must not overlap. Failures leave output unchanged.
#[no_mangle]
pub unsafe extern "C" fn cc_parse_config(bytes: *const u8, len: usize, output: *mut Config) -> u32 {
    if bytes.is_null() || !aligned(output) || len > MAX_CONFIG_BYTES {
        return 1;
    }
    let (Some(input), Some(result)) = (
        region(bytes as usize, len),
        region(output as usize, size_of::<Config>()),
    ) else {
        return 1;
    };
    if overlap(input, result) {
        return 1;
    }
    guarded(|| {
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
    })
}
/// # Safety
/// `config` must be readable, `written` writable, and `output` writable for
/// `capacity` bytes. All regions must be disjoint. Null output is permitted only
/// with zero capacity for a size query. No terminating NUL is written. Status 4
/// writes the required byte count but leaves output unchanged; other errors
/// leave both outputs unchanged. No ownership is retained after this call.
#[no_mangle]
pub unsafe extern "C" fn cc_serialize_config(
    config: *const Config,
    output: *mut u8,
    capacity: usize,
    written: *mut usize,
) -> u32 {
    if !aligned(config)
        || !aligned(written)
        || capacity > MAX_CONFIG_BYTES
        || (output.is_null() && capacity != 0)
    {
        return 1;
    }
    let (Some(input), Some(count), Some(bytes)) = (
        region(config as usize, size_of::<Config>()),
        region(written as usize, size_of::<usize>()),
        region(output as usize, capacity),
    ) else {
        return 1;
    };
    if overlap(input, count) || overlap(input, bytes) || overlap(count, bytes) {
        return 1;
    }
    guarded(|| {
        let Ok(text) = serialize_config(&unsafe { config.read() }) else {
            return 3;
        };
        if capacity < text.len() {
            unsafe {
                written.write(text.len());
            }
            return 4;
        }
        unsafe {
            std::ptr::copy_nonoverlapping(text.as_ptr(), output, text.len());
            written.write(text.len());
        }
        0
    })
}
