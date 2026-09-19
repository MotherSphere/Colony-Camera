//! Independent import of the public SmoothCam JSON data schema.
//! No source algorithm or game implementation is reused here.
use crate::advanced::{AdvancedProfile, Follow, ThirdOptions};
use crate::Config;
use serde::de::{self, MapAccess, SeqAccess, Visitor};
use serde::{Deserialize, Deserializer};
use serde_json::{Map, Value};
use std::collections::HashSet;
use std::fmt;

pub const MAX_SMOOTHCAM_BYTES: usize = 512 * 1024;
pub const MAX_SMOOTHCAM_REPORT_BYTES: usize = 1024 * 1024;
const MAX_REPORT_PATH_BYTES: usize = 4096;
const TRUNCATED: &str = "Report truncated: output or path limit reached; remaining field accounting omitted. Validation still covers the entire input; keep the source JSON.\n";

#[derive(Default)]
struct Report {
    text: String,
    truncated: bool,
}
impl Report {
    fn new(text: &str) -> Self {
        let mut report = Self::default();
        report.append(format_args!("{text}"));
        report
    }
    fn truncate(&mut self) {
        if !self.truncated {
            self.text.push_str(TRUNCATED);
            self.truncated = true;
        }
    }
    fn append(&mut self, args: fmt::Arguments<'_>) {
        if self.truncated {
            return;
        }
        let start = self.text.len();
        // Stream formatting into the budget. In particular, never materialize
        // an arbitrary JSON value or repeated path into an unbounded String.
        if fmt::write(self, args).is_err() {
            self.text.truncate(start);
            self.truncate();
        }
    }
}
impl fmt::Write for Report {
    fn write_str(&mut self, text: &str) -> fmt::Result {
        let remaining = MAX_SMOOTHCAM_REPORT_BYTES - TRUNCATED.len() - self.text.len();
        if text.len() > remaining {
            return Err(fmt::Error);
        }
        self.text.push_str(text);
        Ok(())
    }
}

#[derive(Debug)]
pub struct ImportResult {
    pub config: Config,
    pub report: String,
}

// serde_json supplies the parser, depth limit and number grammar. This visitor
// adds duplicate detection before a JSON object can discard an earlier value.
struct UniqueValue(Value);
impl<'de> Deserialize<'de> for UniqueValue {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct UniqueVisitor;
        impl<'de> Visitor<'de> for UniqueVisitor {
            type Value = UniqueValue;
            fn expecting(&self, f: &mut fmt::Formatter) -> fmt::Result {
                f.write_str("JSON without duplicate object keys")
            }
            fn visit_bool<E: de::Error>(self, v: bool) -> Result<Self::Value, E> {
                Ok(UniqueValue(Value::Bool(v)))
            }
            fn visit_i64<E: de::Error>(self, v: i64) -> Result<Self::Value, E> {
                Ok(UniqueValue(v.into()))
            }
            fn visit_u64<E: de::Error>(self, v: u64) -> Result<Self::Value, E> {
                Ok(UniqueValue(v.into()))
            }
            fn visit_f64<E: de::Error>(self, v: f64) -> Result<Self::Value, E> {
                serde_json::Number::from_f64(v)
                    .map(|n| UniqueValue(Value::Number(n)))
                    .ok_or_else(|| E::custom("nonfinite number"))
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<Self::Value, E> {
                Ok(UniqueValue(Value::String(v.into())))
            }
            fn visit_string<E: de::Error>(self, v: String) -> Result<Self::Value, E> {
                Ok(UniqueValue(Value::String(v)))
            }
            fn visit_unit<E: de::Error>(self) -> Result<Self::Value, E> {
                Ok(UniqueValue(Value::Null))
            }
            fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Self::Value, A::Error> {
                let mut values = Vec::new();
                while let Some(UniqueValue(value)) = seq.next_element()? {
                    values.push(value);
                }
                Ok(UniqueValue(Value::Array(values)))
            }
            fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Self::Value, A::Error> {
                let mut values = Map::new();
                while let Some(key) = map.next_key::<String>()? {
                    if values.contains_key(&key) {
                        return Err(de::Error::custom(format!("duplicate key {key:?}")));
                    }
                    let UniqueValue(value) = map.next_value()?;
                    values.insert(key, value);
                }
                Ok(UniqueValue(Value::Object(values)))
            }
        }
        deserializer.deserialize_any(UniqueVisitor)
    }
}

const MAPPED: &str = "Supported (approximate behavior)";
const STORED: &str = "Unsupported state: preserved, unavailable in the current bridge";
const INACTIVE: &str = "Supported parameter, inactive; value not retained separately";
const UNSUPPORTED: &str = "Unsupported: not applied";

fn path(parent: &str, key: &str) -> String {
    if !key.is_empty() && key.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_') {
        format!("{parent}.{key}")
    } else {
        format!("{parent}[{}]", Value::String(key.into()))
    }
}

fn unknown_field(value: &Value, parent: &str, key: &str, report: &mut Report) {
    if report.truncated {
        return;
    }
    // JSON escaping needs at most six bytes per input byte. Check this upper
    // bound before path() can allocate or clone an attacker-controlled key.
    if parent
        .len()
        .saturating_add(key.len().saturating_mul(6))
        .saturating_add(4)
        > MAX_REPORT_PATH_BYTES
    {
        report.truncate();
        return;
    }
    unknown(value, &path(parent, key), report);
}

fn unknown(value: &Value, path: &str, report: &mut Report) {
    if report.truncated {
        return;
    }
    match value {
        Value::Object(fields) if !fields.is_empty() => {
            report.append(format_args!("Unsupported unknown: {path} (object)\n"));
            for (key, value) in fields {
                if report.truncated {
                    break;
                }
                unknown_field(value, path, key, report);
            }
        }
        Value::Array(values) if !values.is_empty() => {
            report.append(format_args!("Unsupported unknown: {path} (array)\n"));
            for (i, value) in values.iter().enumerate() {
                if report.truncated {
                    break;
                }
                if path.len() + 24 > MAX_REPORT_PATH_BYTES {
                    report.truncate();
                    break;
                }
                unknown(value, &format!("{path}[{i}]"), report);
            }
        }
        _ => report.append(format_args!("Unsupported unknown: {path} = {value}\n")),
    }
}

struct Object<'a, 'r> {
    fields: Option<&'a Map<String, Value>>,
    path: String,
    seen: HashSet<String>,
    status: &'static str,
    report: &'r mut Report,
}
impl<'a, 'r> Object<'a, 'r> {
    fn new(
        value: Option<&'a Value>,
        path: String,
        status: &'static str,
        report: &'r mut Report,
    ) -> Result<Self, String> {
        let fields = value
            .map(|v| {
                v.as_object()
                    .ok_or_else(|| format!("{path}: expected object"))
            })
            .transpose()?;
        if fields.is_some() {
            report.append(format_args!("{status}: {path} (object)\n"));
        }
        Ok(Self {
            fields,
            path,
            seen: HashSet::new(),
            status,
            report,
        })
    }
    fn get(&mut self, key: &str) -> Option<&'a Value> {
        let value = self.fields.and_then(|fields| fields.get(key))?;
        self.seen.insert(key.into());
        self.report.append(format_args!(
            "{}: {} = {value}\n",
            self.status,
            path(&self.path, key)
        ));
        Some(value)
    }
    fn contains(&self, key: &str) -> bool {
        self.fields.is_some_and(|fields| fields.contains_key(key))
    }
    fn object(&mut self, key: &str, status: &'static str) -> Result<Object<'a, '_>, String> {
        self.seen.insert(key.into());
        let value = self.fields.and_then(|fields| fields.get(key));
        Object::new(value, path(&self.path, key), status, self.report)
    }
    fn boolean(&mut self, key: &str, default: bool) -> Result<bool, String> {
        self.get(key).map_or(Ok(default), |v| {
            v.as_bool()
                .ok_or_else(|| format!("{}: expected boolean", path(&self.path, key)))
        })
    }
    fn number(&mut self, key: &str, default: f32, min: f64, max: f64) -> Result<f32, String> {
        let Some(value) = self.get(key) else {
            return Ok(default);
        };
        let number = value
            .as_f64()
            .ok_or_else(|| format!("{}: expected number", path(&self.path, key)))?;
        if !number.is_finite() || number < min || number > max {
            return Err(format!(
                "{}: expected finite number in {min}..={max}",
                path(&self.path, key)
            ));
        }
        Ok(number as f32)
    }
    fn integer(&mut self, key: &str, default: i64, min: i64, max: i64) -> Result<i64, String> {
        let Some(value) = self.get(key) else {
            return Ok(default);
        };
        let number = value
            .as_i64()
            .ok_or_else(|| format!("{}: expected integer", path(&self.path, key)))?;
        if !(min..=max).contains(&number) {
            return Err(format!(
                "{}: expected integer in {min}..={max}",
                path(&self.path, key)
            ));
        }
        Ok(number)
    }
    fn curve(&mut self, key: &str, default: u32) -> Result<u32, String> {
        self.integer(key, default as i64, 0, 21).map(|v| v as u32)
    }
    fn finish(self) {
        if let Some(fields) = self.fields {
            for (key, value) in fields {
                if self.report.truncated {
                    break;
                }
                if !self.seen.contains(key) {
                    unknown_field(value, &self.path, key, self.report);
                }
            }
        }
    }
}

fn follow(r: &mut Object<'_, '_>, keys: [&str; 4], default: Follow) -> Result<Follow, String> {
    let value = Follow {
        enabled: default.enabled,
        curve: r.curve(keys[0], default.curve)?,
        min_rate: r.number(keys[1], default.min_rate, 0.0, 1.0)?,
        max_rate: r.number(keys[2], default.max_rate, 0.0, 1.0)?,
        distance: r.number(keys[3], default.distance, 0.001, 100_000.0)?,
    };
    if value.min_rate > value.max_rate {
        return Err(format!("{}: {} exceeds {}", r.path, keys[1], keys[2]));
    }
    Ok(value)
}
const WORLD_KEYS: [&str; 4] = [
    "currentScalar",
    "minCameraFollowRate",
    "maxCameraFollowRate",
    "zoomMaxSmoothingDistance",
];
const LOCAL_KEYS: [&str; 4] = [
    "separateLocalScalar",
    "localMinFollowRate",
    "localMaxFollowRate",
    "localMaxSmoothingDistance",
];
const VERTICAL_KEYS: [&str; 4] = [
    "separateZScalar",
    "separateZMinFollowRate",
    "separateZMaxFollowRate",
    "separateZMaxSmoothingDistance",
];
const WORLD_DEFAULT: Follow = Follow {
    enabled: 1,
    curve: 13,
    min_rate: 0.25,
    max_rate: 0.66,
    distance: 650.0,
};
const LOCAL_DEFAULT: Follow = Follow {
    enabled: 1,
    curve: 19,
    min_rate: 0.7,
    max_rate: 0.98,
    distance: 60.0,
};
const VERTICAL_DEFAULT: Follow = Follow {
    enabled: 1,
    curve: 13,
    min_rate: 0.4,
    max_rate: 1.0,
    distance: 55.0,
};

fn overrides(
    r: &mut Object<'_, '_>,
    world: Follow,
    local: Follow,
) -> Result<(Follow, Follow), String> {
    let status = r.status;
    let override_world = r.boolean("overrideInterp", false)?;
    let override_local = r.boolean("overrideLocalInterp", false)?;
    if !override_world && status == MAPPED {
        r.status = INACTIVE;
    }
    // Missing override members use OffsetGroupScalar's construction defaults,
    // not the current global values or an unrelated previous preset.
    let candidate_world = follow(r, WORLD_KEYS, WORLD_DEFAULT)?;
    r.status = if !override_local && status == MAPPED {
        INACTIVE
    } else {
        status
    };
    let candidate_local = follow(r, LOCAL_KEYS, LOCAL_DEFAULT)?;
    r.status = status;
    Ok((
        if override_world {
            Follow {
                enabled: world.enabled,
                ..candidate_world
            }
        } else {
            world
        },
        if override_local {
            Follow {
                enabled: local.enabled,
                ..candidate_local
            }
        } else {
            local
        },
    ))
}

const GROUPS: [&str; 14] = [
    "standing",
    "walking",
    "running",
    "sprinting",
    "sneaking",
    "swimming",
    "bowAim",
    "sitting",
    "horseback",
    "dragon",
    "vampireLord",
    "werewolf",
    "userDefined",
    "vanity",
];
const STANCES: [[&str; 6]; 5] = [
    [
        "sideOffset",
        "zoomOffset",
        "upOffset",
        "fovOffset",
        "interp",
        "interpConf",
    ],
    [
        "combatMeleeSideOffset",
        "combatMeleeZoomOffset",
        "combatMeleeUpOffset",
        "combatMeleeFOVOffset",
        "interpMeleeCombat",
        "interpMeleeConf",
    ],
    [
        "combatRangedSideOffset",
        "combatRangedZoomOffset",
        "combatRangedUpOffset",
        "combatRangedFOVOffset",
        "interpRangedCombat",
        "interpRangedConf",
    ],
    [
        "combatMagicSideOffset",
        "combatMagicZoomOffset",
        "combatMagicUpOffset",
        "combatMagicFOVOffset",
        "interpMagicCombat",
        "interpMagicConf",
    ],
    [
        "horseSideOffset",
        "horseZoomOffset",
        "horseUpOffset",
        "horseFOVOffset",
        "interpHorseback",
        "interpHorsebackConf",
    ],
];

fn group(
    r: &mut Object<'_, '_>,
    profiles: &mut [AdvancedProfile],
    world: Follow,
    local: Follow,
    vertical: Follow,
    separate_z: bool,
) -> Result<(), String> {
    let status = r.status;
    for (stance, keys) in STANCES.iter().enumerate() {
        r.status = if stance == 4 { UNSUPPORTED } else { status };
        let offset = [
            r.number(keys[0], 25.0, -1000.0, 1000.0)?,
            r.number(keys[1], 0.0, -1000.0, 1000.0)?,
            r.number(keys[2], 0.0, -1000.0, 1000.0)?,
        ];
        let fov = r.number(keys[3], 0.0, -60.0, 60.0)?;
        let enabled = r.boolean(keys[4], true)? as u32;
        let mut scalar = r.object(keys[5], r.status)?;
        let (mut world, local) = overrides(&mut scalar, world, local)?;
        scalar.finish();
        world.enabled *= enabled;
        let vertical = if separate_z {
            Follow {
                enabled: vertical.enabled * enabled,
                ..vertical
            }
        } else {
            world
        };
        if stance < 4 {
            profiles[stance] = AdvancedProfile {
                offset,
                fov,
                world,
                local,
                vertical,
            };
        }
    }
    r.status = status;
    Ok(())
}

fn unsupported(r: &mut Object<'_, '_>) -> Result<(), String> {
    r.status = UNSUPPORTED;
    for (key, default) in [
        ("use3DBowAimCrosshair", true),
        ("use3DMagicCrosshair", true),
        ("hideNonCombatCrosshair", false),
        ("hideCrosshairMeleeCombat", false),
        ("enableCrosshairSizeManip", true),
        ("useWorldCrosshair", true),
        ("worldCrosshairDepthTest", false),
        ("offsetStealthMeter", false),
        ("alwaysOffsetStealthMeter", false),
        ("use3DPicker", false),
        ("onlyShowCrosshairOnHit", false),
        ("useArrowPrediction", true),
        ("drawArrowArc", true),
        ("useProjectileFixes", true),
        ("disableDeltaTime", false),
        ("enableCrashDumps", false),
    ] {
        r.boolean(key, default)?;
    }
    for (key, default, min, max) in [
        ("crosshairNPCHitGrowSize", 16.0, 0.0, 10000.0),
        ("crosshairMinDistSize", 16.0, 0.0, 10000.0),
        ("crosshairMaxDistSize", 24.0, 0.0, 10000.0),
        ("crosshairPickRadius", 16.0, 0.0, 10000.0),
        ("stealthMeterXOffset", 0.0, -10000.0, 10000.0),
        ("stealthMeterYOffset", 250.0, -10000.0, 10000.0),
        ("maxArrowPredictionRange", 10000.0, 0.0, 1e8),
        ("customZOffset", 0.0, -1000.0, 1000.0),
        ("minCameraFollowDistance", 80.0, 0.0, 10000.0),
        ("zoomMul", 500.0, 0.0, 10000.0),
        ("globalInterpDisableSmoothing", 2.0, 0.0, 60.0),
        ("globalInterpOverrideSmoothing", 1.5, 0.0, 60.0),
        ("localInterpOverrideSmoothing", 2.0, 0.0, 60.0),
    ] {
        r.number(key, default, min, max)?;
    }
    for key in [
        "nextPresetKey",
        "shoulderSwapKey",
        "modToggleKey",
        "applyZOffsetKey",
        "toggleUserDefinedOffsetGroupKey",
    ] {
        r.integer(key, -1, -1, 281)?;
    }
    for key in [
        "globalInterpDisableMehtod",
        "globalInterpOverrideMethod",
        "localInterpOverrideMethod",
    ] {
        r.curve(key, 0)?;
    }
    r.integer("worldCrosshairType", 1, 0, 2)?;
    r.integer("dialogueMode", 1, 0, 3)?;
    let mut color = r.object("arrowArcColor", UNSUPPORTED)?;
    for key in ["r", "g", "b", "a"] {
        color.number(key, if key == "a" { 200.0 } else { 255.0 }, 0.0, 255.0)?;
    }
    color.finish();
    let mut dialogue = r.object("oblivionDialogue", UNSUPPORTED)?;
    dialogue.number("fovOffset", -30.0, -60.0, 60.0)?;
    dialogue.number("zoomInDuration", 1.0, 0.0, 60.0)?;
    dialogue.number("zoomOutDuration", 1.0, 0.0, 60.0)?;
    dialogue.boolean("runInFirstPerson", true)?;
    dialogue.finish();
    let mut dialogue = r.object("faceToFaceDialogue", UNSUPPORTED)?;
    for (key, default) in [("sideOffset", 30.0), ("upOffset", 0.0), ("zoomOffset", 0.0)] {
        dialogue.number(key, default, -1000.0, 1000.0)?;
    }
    for (key, default) in [
        ("rotationDuration", 0.25),
        ("zoomInDuration", 1.0),
        ("zoomOutDuration", 1.0),
    ] {
        dialogue.number(key, default, 0.0, 60.0)?;
    }
    dialogue.boolean("faceToFaceNoSwitch", false)?;
    dialogue.boolean("forceThirdPerson", false)?;
    dialogue.finish();
    r.status = MAPPED;
    Ok(())
}

fn parse_options(r: &mut Object<'_, '_>) -> Result<ThirdOptions, String> {
    let enabled = !r.boolean("modDisabled", false)?;
    let interp = r.boolean("enableInterp", true)? as u32;
    let mut world = follow(r, WORLD_KEYS, WORLD_DEFAULT)?;
    world.enabled = interp;
    let mut local = follow(r, LOCAL_KEYS, LOCAL_DEFAULT)?;
    local.enabled = r.boolean("separateLocalInterp", true)? as u32;
    if r.contains("localScalarRate") {
        let rate = r.number("localScalarRate", 1.0, 0.0, 1.0)?;
        local.min_rate = rate;
        local.max_rate = rate;
        // The public loader gives the old field priority, including ignoring
        // localMaxSmoothingDistance; modern fields remain validated/reported.
        local.distance = LOCAL_DEFAULT.distance;
        r.report.append(format_args!("Legacy localScalarRate takes precedence; modern local rates and distance are inactive.\n"));
    }
    let mut vertical = follow(r, VERTICAL_KEYS, VERTICAL_DEFAULT)?;
    let separate_z = r.boolean("separateZInterp", true)?;
    vertical.enabled = interp;
    if !separate_z {
        r.report.append(format_args!("separateZInterp=false: vertical follow inherits each stance world settings; separate-Z parameters are inactive. Colony still evaluates vertical lag separately, so nonlinear world/Z coupling remains approximate.\n"));
    }
    let mut options = ThirdOptions {
        enabled: enabled as u32,
        group_mask: 127,
        offset_curve: r.curve("offsetScalar", 15)?,
        zoom_curve: r.curve("zoomScalar", 0)?,
        fov_curve: r.curve("fovScalar", 3)?,
        offset_duration: r.number("offsetInterpDurationSecs", 1.0, 0.0, 10.0)?,
        zoom_duration: r.number("zoomInterpDurationSecs", 0.06, 0.0, 10.0)?,
        fov_duration: r.number("fovInterpDurationSecs", 1.0, 0.0, 10.0)?,
        pitch_enabled: r.boolean("enablePitchZoom", false)? as u32,
        pitch_after: r.boolean("pitchZoomAfterInterp", false)? as u32,
        pitch_curve: r.curve("pitchZoomMethod", 14)?,
        pitch_max_angle: r.number("pitchZoomMaxAngle", 90.0, 0.001, 90.0)?,
        pitch_zoom: r.number("pitchZoomMax", 100.0, -1000.0, 1000.0)?,
        mirror_clamp: r.boolean("swapXClamping", true)? as u32,
        ..ThirdOptions::default()
    };
    for (key, duration) in [
        ("enableOffsetInterpolation", &mut options.offset_duration),
        ("enableZoomInterpolation", &mut options.zoom_duration),
        ("enableFOVInterpolation", &mut options.fov_duration),
    ] {
        if !r.boolean(key, true)? {
            *duration = 0.0;
            r.report.append(format_args!(
                "{key}=false: transition snaps; inactive duration is not retained separately.\n"
            ));
        }
    }
    options.clamp_mask = 0;
    for (i, (axis, min, max)) in [("X", -75.0, 35.0), ("Y", -150.0, 0.0), ("Z", -60.0, 60.0)]
        .into_iter()
        .enumerate()
    {
        if r.boolean(&format!("cameraDistanceClamp{axis}Enable"), true)? {
            options.clamp_mask |= 1 << i;
        }
        options.clamp_min[i] = r.number(
            &format!("cameraDistanceClamp{axis}Min"),
            min,
            -10_000.0,
            0.0,
        )?;
        options.clamp_max[i] =
            r.number(&format!("cameraDistanceClamp{axis}Max"), max, 0.0, 10_000.0)?;
        if options.clamp_min[i] > options.clamp_max[i] {
            return Err(format!(
                "{}: cameraDistanceClamp{axis}Min exceeds maximum",
                r.path
            ));
        }
    }
    for (index, name) in GROUPS.iter().enumerate() {
        let mut group_source = r.object(name, if index < 7 { MAPPED } else { STORED })?;
        group(
            &mut group_source,
            &mut options.profiles[index * 4..index * 4 + 4],
            world,
            local,
            vertical,
            separate_z,
        )?;
        group_source.finish();
    }
    unsupported(r)?;
    Ok(options)
}

/// Parse without filesystem or global state changes. Errors never publish a
/// partial configuration. Only advanced third-person settings and its switch
/// change; master enablement, keyboard bindings and first-person data survive.
pub fn import_smoothcam(text: &str, current: Config) -> Result<ImportResult, String> {
    if text.len() > MAX_SMOOTHCAM_BYTES {
        return Err("SmoothCam JSON exceeds 512 KiB".into());
    }
    if !current.valid() {
        return Err("current configuration is invalid".into());
    }
    let UniqueValue(value) =
        serde_json::from_str(text).map_err(|e| format!("SmoothCam JSON: {e}"))?;
    let mut report = Report::new(
        "SmoothCam import: approximate behavioral compatibility, not exact parity.\n\
         Supported: camera offsets, FOV, easing, follow settings, transitions, clamps and pitch zoom.\n\
         Source bone/focus anchors differ: Colony uses its native anchor and compensates native shoulder offsets.\n\
         World/local/vertical follow and clamping are approximations; source zoom scaling and interpolation-override transitions are unsupported.\n\
         Pitch direction and bowAim/sneaking stance selection can differ from the source camera.\n\
         Missing fields use public JSON construction defaults (not the reset-menu preset or previous Colony values).\n\
         Special states sitting, horseback, dragon, vampireLord, werewolf, userDefined and vanity are preserved but unavailable; only groups 0..6 run.\n\
         Horse-specific subprofiles, aiming/crosshair/picker, projectile, dialogue, custom-Z controls and source keybindings are unsupported.\n\
         Unsupported/unknown and inactive values are listed below, not stored in Colony settings; keep the source JSON.\n\
         Master enablement, first-person settings, legacy profiles and keyboard bindings are preserved.\n",
    );
    let mut root = Object::new(Some(&value), "$".into(), MAPPED, &mut report)?;
    let third = if root.contains("config") || root.contains("name") {
        if !root.contains("config") {
            return Err("preset wrapper is missing config".into());
        }
        if let Some(name) = root.get("name") {
            if !name.is_string() {
                return Err("$.name: expected string".into());
            }
        }
        let mut config = root.object("config", MAPPED)?;
        let third = parse_options(&mut config)?;
        config.finish();
        third
    } else {
        parse_options(&mut root)?
    };
    root.finish();
    let config = Config {
        third,
        third_person_enabled: third.enabled,
        ..current
    };
    if !config.valid() {
        return Err("imported settings exceed Colony's supported ranges".into());
    }
    Ok(ImportResult {
        config,
        report: report.text,
    })
}
