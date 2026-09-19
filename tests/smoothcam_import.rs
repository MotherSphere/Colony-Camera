use colony_camera::{smoothcam_import::import_smoothcam, Config};
use serde_json::{json, Value};

fn import(value: Value) -> colony_camera::smoothcam_import::ImportResult {
    import_smoothcam(&value.to_string(), Config::default()).unwrap()
}

fn assert_accounted(value: &Value, path: &str, report: &str) {
    match value {
        Value::Object(fields) if !fields.is_empty() => {
            for (key, value) in fields {
                let child = if !key.is_empty()
                    && key.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_')
                {
                    format!("{path}.{key}")
                } else {
                    format!("{path}[{}]", Value::String(key.clone()))
                };
                assert_accounted(value, &child, report);
            }
        }
        Value::Array(values) if !values.is_empty() => {
            for (i, value) in values.iter().enumerate() {
                assert_accounted(value, &format!("{path}[{i}]"), report);
            }
        }
        Value::Object(_) => {
            assert!(
                report.contains(&format!("{path} (object)"))
                    || report.contains(&format!("{path} = {{}}")),
                "missing {path}"
            );
        }
        _ => {
            let expected = format!(": {path} = {value}");
            assert_eq!(
                report
                    .lines()
                    .filter(|line| line.ends_with(&expected))
                    .count(),
                1,
                "unaccounted or duplicated {path}"
            );
        }
    }
}

#[test]
fn real_schema_shape_maps_stances_overrides_and_preserves_existing_settings() {
    let mut current = Config {
        enabled: 0,
        first_person_enabled: 1,
        keys: [80, 81, 82],
        menu_key: 83,
        body_alignment: colony_camera::BodyAlignmentOptions {
            body_backset: 19.0,
            ..Default::default()
        },
        ..Config::default()
    };
    current.profiles[0].offset = [3.0, 4.0, 5.0];
    let source = json!({"name":"Independent fixture", "config": {
        "currentScalar":13, "minCameraFollowRate":0.2, "maxCameraFollowRate":0.8,
        "separateLocalScalar":19, "localMinFollowRate":0.6, "localMaxFollowRate":0.9,
        "standing": {
            "sideOffset":22.0,"upOffset":-7.0,"zoomOffset":12.0,"fovOffset":-4.0,
            "combatMeleeSideOffset":31.0,"combatRangedSideOffset":42.0,"combatMagicSideOffset":53.0,
            "interpConf":{"overrideInterp":true,"currentScalar":4,"minCameraFollowRate":0.3,"maxCameraFollowRate":0.7,"zoomMaxSmoothingDistance":111.0,
                          "overrideLocalInterp":true,"separateLocalScalar":1,"localMinFollowRate":0.4,"localMaxFollowRate":0.8,"localMaxSmoothingDistance":42.0},
            "interpMeleeCombat":false
        },
        "modToggleKey":-1,"shoulderSwapKey":12,"nextPresetKey":15,"applyZOffsetKey":20,
        "enablePitchZoom":true,"pitchZoomMaxAngle":80.0,"pitchZoomMax":45.0,"pitchZoomAfterInterp":true,"pitchZoomMethod":14,
        "offsetScalar":15,"offsetInterpDurationSecs":0.4,"enableZoomInterpolation":false,
        "cameraDistanceClampXMin":-55.0,"cameraDistanceClampXMax":25.0,"cameraDistanceClampYEnable":false
    }});
    let result = import_smoothcam(&source.to_string(), current).unwrap();
    let p = result.config.third.profiles[0];
    assert_eq!(p.offset, [22.0, 12.0, -7.0]);
    assert_eq!(p.fov, -4.0);
    assert_eq!(
        (
            p.world.curve,
            p.world.min_rate,
            p.world.max_rate,
            p.world.distance
        ),
        (4, 0.3, 0.7, 111.0)
    );
    assert_eq!(
        (
            p.local.curve,
            p.local.min_rate,
            p.local.max_rate,
            p.local.distance
        ),
        (1, 0.4, 0.8, 42.0)
    );
    assert_eq!(result.config.third.profiles[1].offset[0], 31.0);
    assert_eq!(result.config.third.profiles[2].offset[0], 42.0);
    assert_eq!(result.config.third.profiles[3].offset[0], 53.0);
    assert_eq!(result.config.third.profiles[1].world.enabled, 0);
    assert_eq!(result.config.third.profiles[1].local.enabled, 1);
    assert_eq!(result.config.third.profiles[1].vertical.enabled, 0);
    assert_eq!(result.config.third.profiles[2].world.min_rate, 0.2);
    assert_eq!(result.config.third.pitch_enabled, 1);
    assert_eq!(result.config.third.pitch_after, 1);
    assert_eq!(result.config.third.pitch_max_angle, 80.0);
    assert_eq!(result.config.third.pitch_zoom, 45.0);
    assert_eq!(result.config.third.pitch_curve, 14);
    assert_eq!(result.config.third.offset_duration, 0.4);
    assert_eq!(result.config.third.zoom_duration, 0.0);
    assert_eq!(result.config.third.clamp_mask, 5);
    assert_eq!(result.config.third.clamp_min[0], -55.0);
    assert_eq!(result.config.third.clamp_max[0], 25.0);
    let mut preserved = result.config;
    preserved.third = current.third;
    preserved.third_person_enabled = current.third_person_enabled;
    assert_eq!(preserved, current);
    assert!(result.report.contains("approximate"));
    assert!(result.report.contains("anchor"));
    assert!(result.report.contains("native"));
}

#[test]
fn partial_presets_use_schema_construction_defaults_not_previous_preset() {
    let previous = import(
        json!({"standing":{"sideOffset":99.0},"minCameraFollowRate":0.6,"pitchZoomMax":222.0}),
    )
    .config;
    let result = import_smoothcam("{}", previous).unwrap().config;
    let baseline = import(json!({})).config.third;
    assert_eq!(result.third, baseline);
    assert_eq!(baseline.enabled, 1);
    assert_eq!(baseline.group_mask, 127);
    for p in baseline.profiles {
        assert_eq!(p.offset, [25.0, 0.0, 0.0]);
        assert_eq!(p.fov, 0.0);
        assert_eq!(
            (
                p.world.enabled,
                p.world.curve,
                p.world.min_rate,
                p.world.max_rate,
                p.world.distance
            ),
            (1, 13, 0.25, 0.66, 650.0)
        );
        assert_eq!(
            (
                p.local.enabled,
                p.local.curve,
                p.local.min_rate,
                p.local.max_rate,
                p.local.distance
            ),
            (1, 19, 0.7, 0.98, 60.0)
        );
        assert_eq!(
            (
                p.vertical.enabled,
                p.vertical.curve,
                p.vertical.min_rate,
                p.vertical.max_rate,
                p.vertical.distance
            ),
            (1, 13, 0.4, 1.0, 55.0)
        );
    }
    assert_eq!(
        (
            baseline.offset_curve,
            baseline.zoom_curve,
            baseline.fov_curve
        ),
        (15, 0, 3)
    );
    assert_eq!(
        (
            baseline.offset_duration,
            baseline.zoom_duration,
            baseline.fov_duration
        ),
        (1.0, 0.06, 1.0)
    );
    assert_eq!(baseline.clamp_min, [-75.0, -150.0, -60.0]);
    assert_eq!(baseline.clamp_max, [35.0, 0.0, 60.0]);
    assert_eq!(
        (
            baseline.pitch_enabled,
            baseline.pitch_after,
            baseline.pitch_curve
        ),
        (0, 0, 14)
    );
    assert_eq!(
        (
            baseline.pitch_max_angle,
            baseline.pitch_zoom,
            baseline.mirror_clamp
        ),
        (90.0, 100.0, 1)
    );
}

#[test]
fn all_groups_are_preserved_but_special_states_are_reported_unavailable() {
    let groups = [
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
    let mut source = json!({});
    for (i, group) in groups.iter().enumerate() {
        source[group] = json!({"sideOffset":i+1,"combatMeleeSideOffset":i+21,"combatRangedSideOffset":i+41,"combatMagicSideOffset":i+61});
    }
    let result = import(source);
    for (i, group) in groups.iter().enumerate() {
        for stance in 0..4 {
            assert_eq!(
                result.config.third.profiles[i * 4 + stance].offset[0],
                (i + 1 + stance * 20) as f32
            );
        }
        if i >= 7 {
            assert!(
                result
                    .report
                    .lines()
                    .any(|line| line.contains(group) && line.contains("unavailable")),
                "{group}"
            );
        }
    }
    assert_eq!(result.config.third.group_mask, 127);
}

#[test]
fn accounts_for_unknown_nested_and_unsupported_fields_with_values() {
    let result = import(
        json!({"name":"test", "extra":{"nested":[{"leaf":17},{}]},"config":{
            "standing":{"interpConf":{"newCurve":{"nested":7}},"horseSideOffset":65},
            "useProjectileFixes":true,"use3DBowAimCrosshair":true,"dialogueMode":2,
            "oblivionDialogue":{"fovOffset":-20,"future":{"mode":"new"}},
            "faceToFaceDialogue":{"forceThirdPerson":true},"arrowArcColor":{"r":255,"future":{}},
            "mystery":{"deeper":{"leaf":false}},"empty":[]
        }}),
    );
    for path in [
        "$.extra.nested[0].leaf",
        "$.extra.nested[1]",
        "$.config.standing.interpConf.newCurve.nested",
        "$.config.oblivionDialogue.future.mode",
        "$.config.arrowArcColor.future",
        "$.config.mystery.deeper.leaf",
        "$.config.empty",
    ] {
        assert!(
            result
                .report
                .lines()
                .any(|line| line.contains(path) && line.contains("unknown")),
            "{path}\n{}",
            result.report
        );
    }
    for path in [
        "useProjectileFixes",
        "use3DBowAimCrosshair",
        "dialogueMode",
        "oblivionDialogue.fovOffset",
        "faceToFaceDialogue.forceThirdPerson",
        "standing.horseSideOffset",
    ] {
        assert!(
            result
                .report
                .lines()
                .any(|line| line.contains(path) && line.contains("Unsupported")),
            "{path}"
        );
    }
    assert!(result.report.contains("65"));
}

#[test]
fn rejects_duplicates_even_inside_unknown_objects_and_escaped_names() {
    for text in [
        r#"{"standing":{},"standing":{}}"#,
        r#"{"standing":{"sideOffset":1,"sideOffset":2}}"#,
        r#"{"extra":{"x":1,"x":2}}"#,
        r#"{"extra":[{"x":1,"\u0078":2}]}"#,
        r#"{"config":{},"config":{}}"#,
    ] {
        let error = import_smoothcam(text, Config::default()).err().unwrap();
        assert!(error.contains("duplicate"), "{error}");
    }
}

#[test]
fn rejects_wrong_types_ranges_enums_and_malformed_numbers_atomically() {
    let current = Config::default();
    let before = current;
    for text in [
        "",
        "null",
        "[]",
        "true",
        "{} {}",
        "{",
        r#"{"config":null}"#,
        r#"{"name":4,"config":{}}"#,
        r#"{"name":"missing config"}"#,
        r#"{"enableInterp":1}"#,
        r#"{"standing":[]}"#,
        r#"{"standing":{"sideOffset":"NaN"}}"#,
        r#"{"standing":{"sideOffset":null}}"#,
        r#"{"standing":{"sideOffset":1e999}}"#,
        r#"{"standing":{"sideOffset":NaN}}"#,
        r#"{"standing":{"sideOffset":1000000000}}"#,
        r#"{"currentScalar":22}"#,
        r#"{"currentScalar":-1}"#,
        r#"{"currentScalar":1.5}"#,
        r#"{"currentScalar":"SINE_IN"}"#,
        r#"{"minCameraFollowRate":-0.1}"#,
        r#"{"maxCameraFollowRate":1.1}"#,
        r#"{"minCameraFollowRate":0.9,"maxCameraFollowRate":0.1}"#,
        r#"{"standing":{"interpConf":{"overrideInterp":false,"minCameraFollowRate":0.9,"maxCameraFollowRate":0.1}}}"#,
        r#"{"zoomMaxSmoothingDistance":-1}"#,
        r#"{"offsetInterpDurationSecs":-1}"#,
        r#"{"cameraDistanceClampXMin":30,"cameraDistanceClampXMax":20}"#,
        r#"{"pitchZoomMaxAngle":91}"#,
        r#"{"useProjectileFixes":"true"}"#,
        r#"{"dialogueMode":4}"#,
        r#"{"arrowArcColor":{"r":256}}"#,
        r#"{"oblivionDialogue":{"runInFirstPerson":0}}"#,
        r#"{"shoulderSwapKey":2.5}"#,
        r#"{"unknown":1e999}"#,
    ] {
        assert!(import_smoothcam(text, current).is_err(), "accepted {text}");
        assert_eq!(current, before);
    }
    assert!(import_smoothcam(&" ".repeat(512 * 1024 + 1), current).is_err());
    let deep = format!("{}0{}", "[".repeat(200), "]".repeat(200));
    assert!(import_smoothcam(&deep, current).is_err());
}

#[test]
fn legacy_local_rate_is_migrated_and_inactive_override_values_are_accounted_for() {
    let result = import(
        json!({"localScalarRate":0.42,"localMinFollowRate":0.2,"localMaxFollowRate":0.9,"standing":{"interpConf":{"overrideInterp":false,"minCameraFollowRate":0.1}}}),
    );
    assert_eq!(result.config.third.profiles[0].local.min_rate, 0.42);
    assert_eq!(result.config.third.profiles[0].local.max_rate, 0.42);
    assert!(result.report.contains("inactive"));
}

#[test]
fn explicit_mod_disabled_only_changes_third_person_enablement() {
    let result = import(json!({"modDisabled":true})).config;
    assert_eq!(result.third.enabled, 0);
    assert_eq!(result.third_person_enabled, 0);
    assert_eq!(result.enabled, 1);
}

// Opt-in read-only check: no personal preset or absolute machine path is stored in the repository.
#[test]
fn external_real_file_when_requested() {
    let Ok(path) = std::env::var("SMOOTHCAM_TEST_FILE") else {
        return;
    };
    let text = std::fs::read_to_string(path).unwrap();
    let source: Value = serde_json::from_str(&text).unwrap();
    let result = import_smoothcam(&text, Config::default()).unwrap();
    assert_accounted(&source, "$", &result.report);
    assert!(!result.report.contains("Report truncated:"));
    let source = source.get("config").unwrap_or(&source);
    assert_eq!(
        result.config.third.profiles[0].offset[0],
        source["standing"]["sideOffset"].as_f64().unwrap() as f32
    );
    assert!(
        !result
            .report
            .lines()
            .any(|line| line.starts_with("Unsupported unknown:")),
        "unexpected unknown field"
    );
    assert!(result.config.valid());
    let third = result.config.third;
    eprintln!("Real-file import: {} bytes, {} reported leaf values, {} report bytes, no unknown fields; third enabled={}, group mask={}", text.len(), result.report.lines().filter(|line| line.contains(" = ")).count(), result.report.len(), third.enabled, third.group_mask);
    for (i, name) in [
        "standing",
        "walking",
        "running",
        "sprinting",
        "sneaking",
        "swimming",
        "bowAim",
    ]
    .iter()
    .enumerate()
    {
        eprintln!("{name} neutral: {:?}", third.profiles[i * 4]);
    }
    eprintln!("Transitions: offset={}s/curve{}, zoom={}s/curve{}, FOV={}s/curve{}; clamps {:?}..{:?}, mask={}; pitch enabled={}, max={}, angle={}", third.offset_duration, third.offset_curve, third.zoom_duration, third.zoom_curve, third.fov_duration, third.fov_curve, third.clamp_min, third.clamp_max, third.clamp_mask, third.pitch_enabled, third.pitch_zoom, third.pitch_max_angle);
}

#[test]
fn independent_local_follow_survives_world_disable_and_z_falls_back_to_world() {
    let result = import(json!({"enableInterp":false,"separateLocalInterp":true}));
    assert_eq!(result.config.third.profiles[0].world.enabled, 0);
    assert_eq!(result.config.third.profiles[0].local.enabled, 1);
    assert_eq!(result.config.third.profiles[0].vertical.enabled, 0);
    let result = import(
        json!({"separateZInterp":false,"standing":{"interpConf":{"overrideInterp":true,"currentScalar":5,"minCameraFollowRate":0.2,"maxCameraFollowRate":0.7,"zoomMaxSmoothingDistance":345}}}),
    );
    let p = result.config.third.profiles[0];
    assert_eq!(p.vertical, p.world);
    let p = import(json!({"standing":{"interp":false}}))
        .config
        .third
        .profiles[0];
    assert_eq!(p.world.enabled, 0);
    assert_eq!(p.vertical.enabled, 0);
    assert_eq!(p.local.enabled, 1);
}

#[test]
fn inactive_values_must_still_satisfy_supported_engine_ranges() {
    for source in [
        json!({"standing":{"interpConf":{"overrideInterp":false,"zoomMaxSmoothingDistance":0}}}),
        json!({"standing":{"interpHorsebackConf":{"localMaxSmoothingDistance":0}}}),
        json!({"enableOffsetInterpolation":false,"offsetInterpDurationSecs":11}),
        json!({"separateZInterp":false,"separateZMaxSmoothingDistance":0}),
        json!({"enablePitchZoom":false,"pitchZoomMaxAngle":0}),
    ] {
        assert!(
            import_smoothcam(&source.to_string(), Config::default()).is_err(),
            "{source}"
        );
    }
}

#[test]
fn engine_range_boundaries_are_accepted_without_clamping() {
    let p = import(json!({"zoomMaxSmoothingDistance":100000,"standing":{"sideOffset":1000},"cameraDistanceClampXMin":-10000,"cameraDistanceClampXMax":10000,"offsetInterpDurationSecs":10})).config.third;
    assert_eq!(p.profiles[0].world.distance, 100000.0);
    assert_eq!(p.clamp_min[0], -10000.0);
    assert_eq!(p.clamp_max[0], 10000.0);
    assert_eq!(p.offset_duration, 10.0);
    assert!(import_smoothcam(
        r#"{"zoomMaxSmoothingDistance":0.001,"pitchZoomMaxAngle":0.001}"#,
        Config::default()
    )
    .is_ok());
}

#[test]
fn all_curve_ids_and_override_defaults_survive_persistence() {
    for curve in 0..22 {
        let c = import(json!({"currentScalar":curve,"localMinFollowRate":0.1,"standing":{"interpConf":{"overrideLocalInterp":true}}})).config;
        assert_eq!(c.third.profiles[0].world.curve, curve);
        assert_eq!(c.third.profiles[0].local.min_rate, 0.7);
        assert_eq!(c.third.profiles[1].local.min_rate, 0.1);
        let text = colony_camera::serialize_config(&c).unwrap();
        assert_eq!(colony_camera::parse_config(&text).unwrap(), c);
    }
}

#[test]
fn accounting_cannot_confuse_unknown_paths_with_supported_fields() {
    let source = json!({"name":"\nPreset", "config":{
        "standing":{"sideOffset":18,"interpConf":{"future":{"leaf":3}}},
        "standing.sideOffset":88,"unusual\nkey":{"":null,"[]":[],"objects":[{}, {"x":2}]},
        "arrowArcColor":{"r":20,"g":21,"b":22,"a":23},
        "oblivionDialogue":{"fovOffset":-10},"faceToFaceDialogue":{"zoomOffset":5}
    },"extra":[{"a.b":true}]});
    let result = import(source.clone());
    assert_accounted(&source, "$", &result.report);
    assert_eq!(result.config.third.profiles[0].offset[0], 18.0);
}

#[test]
fn size_limit_is_in_bytes_and_accepts_exactly_the_limit() {
    let text = format!("{}{{}}", " ".repeat(512 * 1024 - 2));
    assert!(import_smoothcam(&text, Config::default()).is_ok());
    let text = format!(r#"{{"unknown":"{}"}}"#, "é".repeat(256 * 1024));
    assert!(text.chars().count() < 512 * 1024);
    assert!(import_smoothcam(&text, Config::default()).is_err());
}

#[test]
fn rejection_names_the_invalid_field_even_when_inactive() {
    for (value, field) in [
        (
            json!({"enableZoomInterpolation":false,"zoomInterpDurationSecs":60}),
            "$.zoomInterpDurationSecs",
        ),
        (json!({"pitchZoomMaxAngle":0}), "$.pitchZoomMaxAngle"),
        (
            json!({"standing":{"interpConf":{"zoomMaxSmoothingDistance":0}}}),
            "$.standing.interpConf.zoomMaxSmoothingDistance",
        ),
        (
            json!({"cameraDistanceClampZMin":10}),
            "$.cameraDistanceClampZMin",
        ),
    ] {
        let error = import_smoothcam(&value.to_string(), Config::default())
            .err()
            .unwrap();
        assert!(error.contains(field), "{error}");
    }
}

#[test]
fn unknown_field_reports_have_a_fixed_output_budget() {
    let source = json!({"standing":{"sideOffset":21}, "future": {
        "k".repeat(500): vec![0; 10_000]
    }})
    .to_string();
    assert!(source.len() < 512 * 1024);
    let result = import_smoothcam(&source, Config::default()).unwrap();
    assert!(
        result.report.len() <= 1024 * 1024,
        "report grew to {} bytes",
        result.report.len()
    );
    assert_eq!(result.report.matches("Report truncated:").count(), 1);
    assert_eq!(result.config.third.profiles[0].offset[0], 21.0);
}

#[test]
fn enormous_parent_keys_do_not_expand_across_array_leaves() {
    let key = "k".repeat(200 * 1024);
    let mut source = json!({"standing": {key: vec![0; 100_000]}, "walking":{"sideOffset":27}});
    let text = source.to_string();
    assert!(text.len() <= 512 * 1024);
    let result = import_smoothcam(&text, Config::default()).unwrap();
    assert!(result.report.len() <= 1024 * 1024);
    assert_eq!(result.report.matches("Report truncated:").count(), 1);
    assert_eq!(result.config.third.profiles[4].offset[0], 27.0);
    // Stopping report traversal must not stop subsequent validation.
    source["walking"]["sideOffset"] = Value::Null;
    let error = import_smoothcam(&source.to_string(), Config::default())
        .err()
        .unwrap();
    assert!(error.contains("$.walking.sideOffset"), "{error}");
}

#[test]
fn escaped_paths_and_unicode_values_obey_the_same_budget() {
    let source = json!({"future": {"é".repeat(250): vec!["é".repeat(50); 3500]}}).to_string();
    assert!(source.len() <= 512 * 1024);
    let result = import_smoothcam(&source, Config::default()).unwrap();
    assert!(result.report.len() <= 1024 * 1024);
    assert_eq!(result.report.matches("Report truncated:").count(), 1);
    let source = json!({"\u{1}".repeat(200_000 / 6): vec![0; 100_000]}).to_string();
    assert!(source.len() <= 512 * 1024);
    let result = import_smoothcam(&source, Config::default()).unwrap();
    assert!(result.report.len() <= 1024 * 1024);
    assert_eq!(result.report.matches("Report truncated:").count(), 1);
}

#[test]
fn imported_standing_distance_keeps_camera_behind_target_at_rest() {
    use colony_camera::advanced::{step_advanced, AdvancedFrame, AdvancedState};
    use colony_camera::Frame;
    // Geometry of the reported preset: +140 is relative to a 250-unit boom,
    // not relative to the engine camera's possibly much shorter current boom.
    let result = import(json!({"minCameraFollowDistance":250.0,"zoomMul":1.0,
        "standing":{"sideOffset":0.0,"zoomOffset":140.0,"upOffset":50.0}}));
    let frame = AdvancedFrame {
        native: Frame {
            position: [0.0, -80.0, 120.0],
            dt: 1.0 / 60.0,
            ..Frame::default()
        },
        focus: [0.0, 0.0, 120.0],
        ..AdvancedFrame::default()
    };
    let mut state = step_advanced(AdvancedState::default(), frame, &result.config.third);
    assert_eq!(state.position, [0.0, -110.0, 170.0]);
    for _ in 0..600 {
        state = step_advanced(state, frame, &result.config.third);
    }
    assert_eq!(state.position, [0.0, -110.0, 170.0]);
}

#[test]
fn imported_height_stays_world_vertical_when_looking_down() {
    use colony_camera::advanced::{step_advanced, AdvancedFrame, AdvancedState};
    use colony_camera::Frame;
    let a = import(json!({"minCameraFollowDistance":250.0,"standing":{"upOffset":0.0}}));
    let b = import(json!({"minCameraFollowDistance":250.0,"standing":{"upOffset":50.0}}));
    let half = (-30.0_f32).to_radians() / 2.0;
    let frame = AdvancedFrame {
        native: Frame {
            position: [0.0, -80.0, 120.0],
            rotation: [half.cos(), half.sin(), 0.0, 0.0],
            ..Frame::default()
        },
        focus: [0.0, 0.0, 120.0],
        ..AdvancedFrame::default()
    };
    let low = step_advanced(AdvancedState::default(), frame, &a.config.third);
    let high = step_advanced(AdvancedState::default(), frame, &b.config.third);
    assert!((high.position[1] - low.position[1]).abs() < 0.001);
    assert!((high.position[2] - low.position[2] - 50.0).abs() < 0.001);
}

#[test]
fn imported_distance_uses_zoom_scale_and_survives_save_reload() {
    use colony_camera::advanced::{step_advanced, AdvancedFrame, AdvancedState};
    use colony_camera::{parse_config, serialize_config, Frame};
    let imported = import(json!({"minCameraFollowDistance":250.0,"zoomMul":20.0,
        "standing":{"sideOffset":0.0,"zoomOffset":140.0,"upOffset":50.0}}));
    let saved = parse_config(&serialize_config(&imported.config).unwrap()).unwrap();
    assert_eq!(saved, imported.config);
    assert_eq!(saved.third.preset_geometry, 1);
    for (zoom, expected) in [(0.0, -110.0), (0.5, -120.0), (2.0, -150.0)] {
        let f = AdvancedFrame {
            native: Frame {
                position: [0.0, -80.0, 120.0],
                ..Frame::default()
            },
            focus: [0.0, 0.0, 120.0],
            zoom,
            ..AdvancedFrame::default()
        };
        let s = step_advanced(AdvancedState::default(), f, &saved.third);
        assert_eq!(s.position, [0.0, expected, 170.0]);
    }
    for bad in [
        json!({"minCameraFollowDistance":-1}),
        json!({"zoomMul":10001}),
    ] {
        assert!(import_smoothcam(&bad.to_string(), Config::default()).is_err());
    }
}
