use colony_camera::advanced::*;
use colony_camera::*;

fn options() -> ThirdOptions {
    let mut o = ThirdOptions {
        enabled: 1,
        ..ThirdOptions::default()
    };
    for p in &mut o.profiles {
        p.world.enabled = 0;
        p.local.enabled = 0;
        p.vertical.enabled = 0;
    }
    o
}
fn frame(dt: f32) -> AdvancedFrame {
    AdvancedFrame {
        native: Frame {
            position: [0.0, -100.0, 0.0],
            dt,
            ..Frame::default()
        },
        ..AdvancedFrame::default()
    }
}
fn near(a: f32, b: f32, tolerance: f32) {
    assert!(
        (a - b).abs() <= tolerance,
        "{a} != {b}, tolerance {tolerance}"
    );
}
#[test]
fn curves_have_distinct_standard_shapes_and_exact_endpoints() {
    // Literal quarter-time values catch enum swaps and duplicated curve families.
    let quarter = [
        0.25,
        0.0625,
        0.4375,
        0.125,
        0.015625,
        0.578125,
        0.0625,
        0.00390625,
        0.68359375,
        0.03125,
        0.0009765625,
        0.7626953,
        0.015625,
        0.07612047,
        0.38268343,
        0.14644662,
        0.03175416,
        0.6614378,
        0.066_987_3,
        0.005524272,
        0.8232233,
        0.015625,
    ];
    for (id, expected) in quarter.into_iter().enumerate() {
        assert_eq!(advanced_curve(id as u32, 0.0), 0.0);
        assert_eq!(advanced_curve(id as u32, 1.0), 1.0);
        near(advanced_curve(id as u32, 0.25), expected, 0.000001);
        let mut previous = 0.0;
        for i in 0..=1000 {
            let x = advanced_curve(id as u32, i as f32 / 1000.0);
            assert!(x >= previous && x <= 1.0);
            previous = x;
        }
    }
}
#[test]
fn transitions_finish_at_their_own_durations_across_frame_rates() {
    for fps in [30, 60, 144] {
        let mut o = options();
        o.offset_duration = 0.5;
        o.zoom_duration = 0.25;
        o.fov_duration = 1.0;
        o.offset_curve = 0;
        o.zoom_curve = 1;
        o.fov_curve = 0;
        o.profiles[4].offset = [100.0, 80.0, -40.0];
        o.profiles[4].fov = 20.0;
        let mut f = frame(1.0 / fps as f32);
        let mut s = step_advanced(AdvancedState::default(), f, &o);
        f.group = 1;
        for i in 1..=fps {
            s = step_advanced(s, f, &o);
            let t = i as f32 / fps as f32;
            near(s.offset[0], 100.0 * (t / 0.5).min(1.0), 0.001);
            near(s.offset[1], 80.0 * (t / 0.25).min(1.0).powi(2), 0.001);
            near(s.fov_delta, 20.0 * t, 0.001);
            if t >= 0.5 {
                assert_eq!(s.offset[0], 100.0);
            }
            if t >= 0.25 {
                assert_eq!(s.offset[1], 80.0);
            }
        }
        assert_eq!(s.fov_delta, 20.0);
        assert_eq!(s.position, [100.0, -20.0, -40.0]);
    }
}
#[test]
fn follow_is_time_based_and_vertical_does_not_use_horizontal_distance() {
    let mut o = options();
    o.profiles[0].world = Follow {
        enabled: 1,
        curve: 0,
        min_rate: 0.01,
        max_rate: 0.01,
        distance: 650.0,
    };
    o.profiles[0].vertical = Follow {
        enabled: 1,
        curve: 0,
        min_rate: 0.02,
        max_rate: 0.02,
        distance: 55.0,
    };
    let run = |fps| {
        let mut f = frame(1.0 / fps as f32);
        let mut s = step_advanced(AdvancedState::default(), f, &o);
        f.focus = [100.0, 0.0, 100.0];
        f.native.position = [100.0, -100.0, 100.0];
        for _ in 0..fps {
            s = step_advanced(s, f, &o);
        }
        s
    };
    let (a, b) = (run(30), run(144));
    near(a.position[0], 45.284336, 0.001);
    near(a.position[2], 70.24468, 0.001);
    for i in 0..3 {
        near(a.position[i], b.position[i], 0.002);
    }
}
#[test]
fn clamps_are_camera_local_and_shoulder_mirroring_reverses_x_bounds() {
    let mut o = options();
    o.profiles[0].world = Follow {
        enabled: 1,
        min_rate: 0.0,
        max_rate: 0.0,
        ..Follow::default()
    };
    o.clamp_mask = 7;
    o.clamp_min = [-2.0, -3.0, -4.0];
    o.clamp_max = [5.0, 6.0, 7.0];
    let mut f = frame(0.01);
    f.native.rotation = [
        std::f32::consts::FRAC_1_SQRT_2,
        0.0,
        0.0,
        std::f32::consts::FRAC_1_SQRT_2,
    ];
    let initial = step_advanced(AdvancedState::default(), f, &o);
    f.focus = [100.0, 100.0, 0.0];
    f.native.position = [100.0, 0.0, 0.0];
    let s = step_advanced(initial, f, &o);
    // World lag [-100,-100] becomes local [-100,+100], limited to [-2,+6].
    near(s.position[0], 94.0, 0.001);
    near(s.position[1], -2.0, 0.001);
    f.shoulder_mirrored = 1;
    o.mirror_clamp = 1;
    let mirrored = step_advanced(initial, f, &o);
    near(mirrored.position[0], 94.0, 0.001);
    near(mirrored.position[1], -5.0, 0.001);
}
#[test]
fn native_shoulders_are_replaced_once_and_pitch_can_bypass_orbit_filter() {
    let mut o = options();
    o.profiles[0].offset = [20.0, 10.0, 30.0];
    o.profiles[0].local = Follow {
        enabled: 1,
        min_rate: 0.0,
        max_rate: 0.0,
        ..Follow::default()
    };
    o.pitch_enabled = 1;
    o.pitch_max_angle = 90.0;
    o.pitch_zoom = 60.0;
    o.pitch_curve = 0;
    o.offset_duration = 0.0;
    let mut f = frame(0.01);
    f.native.position = [10.0, -100.0, 15.0];
    f.native_side = 10.0;
    f.native_up = 15.0;
    let initial = step_advanced(AdvancedState::default(), f, &o);
    assert_eq!(initial.position, [20.0, -90.0, 30.0]);
    f.pitch_degrees = 90.0;
    let before = step_advanced(initial, f, &o);
    assert_eq!(before.position, initial.position);
    o.pitch_after = 1;
    let after = step_advanced(initial, f, &o);
    assert_eq!(after.position, [20.0, -30.0, 30.0]);
    f.shoulder_mirrored = 1;
    f.native_side = -10.0;
    f.native.position[0] = -10.0;
    let mirror = step_advanced(after, f, &o);
    assert_eq!(mirror.position, [-20.0, -30.0, 30.0]);
}
#[test]
fn reset_teleport_corrupt_history_and_disabled_options_never_leak_history() {
    let mut o = options();
    let initial = step_advanced(AdvancedState::default(), frame(0.01), &o);
    let mut f = frame(0.01);
    f.focus[0] = 2000.0;
    f.native.position[0] = 2000.0;
    assert_eq!(step_advanced(initial, f, &o).position, f.native.position);
    let mut corrupt = initial;
    corrupt.anchor[0] = f32::NAN;
    assert_eq!(step_advanced(corrupt, frame(0.01), &o).initialized, 1);
    f.native.rotation = [0.0; 4];
    assert_eq!(step_advanced(initial, f, &o).initialized, 0);
    o.enabled = 0;
    assert_eq!(step_advanced(initial, frame(0.01), &o).initialized, 0);
    o.enabled = 1;
    f = frame(0.01);
    f.group = 13;
    assert_eq!(step_advanced(initial, f, &o).initialized, 0);
}
#[test]
fn advanced_config_roundtrip_is_strict_and_validation_checks_all_profiles() {
    let mut o = options();
    let text = serde_json::to_string(&o).unwrap();
    assert_eq!(serde_json::from_str::<ThirdOptions>(&text).unwrap(), o);
    assert!(serde_json::from_str::<ThirdOptions>(&text.replacen('{', "{\"typo\":0,", 1)).is_err());
    o.profiles[55].local.distance = f32::NAN;
    assert!(!o.valid());
    o = options();
    o.clamp_min[0] = 1.0;
    assert!(!o.valid());
    o = options();
    o.profiles[55].world.curve = 22;
    assert!(!o.valid());
}
#[test]
fn pointer_abi_has_expected_layout_and_rejects_overlap_without_writes() {
    use std::mem::{align_of, offset_of, size_of};
    assert_eq!(
        (size_of::<ThirdOptions>(), align_of::<ThirdOptions>()),
        (4352, 4)
    );
    assert_eq!(size_of::<AdvancedFrame>(), 80);
    assert_eq!(size_of::<AdvancedState>(), 128);
    assert_eq!(offset_of!(AdvancedState, elapsed), 116);
    let o = options();
    let f = frame(0.01);
    let s = AdvancedState::default();
    let mut out = s;
    unsafe {
        assert_eq!(cc_step_advanced(&s, &f, &o, &mut out), 0);
        assert_eq!(out.initialized, 1);
        let pointer = &mut out as *mut AdvancedState;
        assert_eq!(cc_step_advanced(pointer, &f, &o, pointer), 1);
        assert_eq!(out.initialized, 1);
        assert_eq!(cc_step_advanced(std::ptr::null(), &f, &o, &mut out), 1);
        let mut bad = f;
        bad.native.dt = f32::NAN;
        assert_eq!(cc_step_advanced(&s, &bad, &o, &mut out), 3);
        assert_eq!(out.initialized, 1);
    }
}

#[test]
fn collision_feedback_preserves_player_anchor_and_filters_wall_release() {
    let mut o = options();
    o.profiles[0].local = Follow {
        enabled: 1,
        min_rate: 0.01,
        max_rate: 0.01,
        ..Follow::default()
    };
    let f = frame(1.0 / 60.0);
    let mut s = step_advanced(AdvancedState::default(), f, &o);
    // The engine collision pushes the camera from Y=-100 to Y=-40.
    s.orbit[1] += 60.0;
    s.position[1] = -40.0;
    let next = step_advanced(s, f, &o);
    assert_eq!(next.anchor, [0.0; 3]);
    near(next.position[1], -40.6, 0.0001);
    // A wrong bridge feedback into anchor would vanish on this next frame.
    let mut moving = f;
    moving.focus[0] = 10.0;
    moving.native.position[0] = 10.0;
    let next = step_advanced(s, moving, &o);
    assert_eq!(next.anchor, [10.0, 0.0, 0.0]);
    near(next.position[0], 10.0, 0.0001);
    near(next.position[1], -40.6, 0.0001);
}
#[test]
fn orbit_smoothing_is_independent_of_player_world_follow() {
    let mut o = options();
    o.profiles[0].world = Follow {
        enabled: 1,
        min_rate: 0.0,
        max_rate: 0.0,
        ..Follow::default()
    };
    o.profiles[0].local = Follow {
        enabled: 1,
        min_rate: 0.01,
        max_rate: 0.01,
        ..Follow::default()
    };
    let run = |fps| {
        let mut f = frame(1.0 / fps as f32);
        let mut s = step_advanced(AdvancedState::default(), f, &o);
        // Rotate the native orbit by 90 degrees while the player moves in X.
        f.focus = [100.0, 0.0, 0.0];
        f.native.position = [200.0, 0.0, 0.0];
        f.native.rotation = [
            std::f32::consts::FRAC_1_SQRT_2,
            0.0,
            0.0,
            std::f32::consts::FRAC_1_SQRT_2,
        ];
        for _ in 0..fps {
            s = step_advanced(s, f, &o);
        }
        s
    };
    let a = run(30);
    let b = run(144);
    assert_eq!(a.anchor, [0.0; 3]);
    near(a.position[0], 45.284336, 0.001);
    near(a.position[1], -54.715664, 0.001);
    for i in 0..3 {
        near(a.position[i], b.position[i], 0.002);
    }
}
#[test]
fn distance_dependent_response_has_small_actual_timestep_error() {
    let mut o = options();
    o.profiles[0].world = Follow {
        enabled: 1,
        curve: 3,
        min_rate: 0.002,
        max_rate: 0.03,
        distance: 200.0,
    };
    o.profiles[0].vertical = o.profiles[0].world;
    o.profiles[0].local = o.profiles[0].world;
    let run = |fps| {
        let mut f = frame(1.0 / fps as f32);
        let mut s = step_advanced(AdvancedState::default(), f, &o);
        f.focus = [100.0, 0.0, 50.0];
        f.native.position = [200.0, 0.0, 50.0];
        for _ in 0..fps {
            s = step_advanced(s, f, &o);
        }
        s
    };
    let reference = run(960);
    for fps in [30, 60, 144] {
        let s = run(fps);
        assert!(s.position[0] > 20.0 && s.position[0] < 180.0);
        for i in 0..3 {
            near(s.position[i], reference.position[i], 0.08);
        }
    }
}
#[test]
fn retargeting_starts_from_current_value_and_does_not_restart_other_clocks() {
    let mut o = options();
    o.offset_duration = 1.0;
    o.zoom_duration = 1.0;
    o.fov_duration = 1.0;
    o.profiles[4].offset = [100.0, 80.0, 0.0];
    o.profiles[4].fov = 20.0;
    o.profiles[5].offset = [-100.0, 80.0, 0.0];
    o.profiles[5].fov = 20.0;
    let mut f = frame(0.25);
    let mut s = step_advanced(AdvancedState::default(), f, &o);
    f.group = 1;
    s = step_advanced(s, f, &o);
    assert_eq!(s.offset, [25.0, 20.0, 0.0]);
    assert_eq!(s.fov_delta, 5.0);
    f.stance = 1;
    s = step_advanced(s, f, &o);
    assert_eq!(s.offset, [-6.25, 40.0, 0.0]);
    assert_eq!(s.fov_delta, 10.0);
    f.native.dt = 0.0;
    let held = step_advanced(s, f, &o);
    assert_eq!(held.offset, s.offset);
    assert_eq!(held.fov_delta, s.fov_delta);
}
#[test]
fn native_fov_is_immediate_and_its_zero_offset_preserves_extreme_values() {
    let mut o = options();
    o.fov_duration = 0.0;
    o.profiles[0].fov = 10.0;
    let mut f = frame(0.1);
    let s = step_advanced(AdvancedState::default(), f, &o);
    f.native.world_fov = 100.0;
    assert_eq!(step_advanced(s, f, &o).fov, 110.0);
    o.profiles[0].fov = 0.0;
    for native in [1.0, 170.0, 179.0] {
        f.native.world_fov = native;
        assert_eq!(step_advanced(s, f, &o).fov, native);
    }
}
#[test]
fn group_and_stance_select_all_profiles_without_aliasing() {
    let mut o = options();
    o.group_mask = 0x3fff;
    for (i, p) in o.profiles.iter_mut().enumerate() {
        p.offset[0] = i as f32;
    }
    for group in 0..14 {
        for stance in 0..4 {
            let f = AdvancedFrame {
                group,
                stance,
                ..frame(0.01)
            };
            assert_eq!(
                step_advanced(AdvancedState::default(), f, &o).position[0],
                (group * 4 + stance) as f32
            );
        }
    }
    for (group, stance) in [(14, 0), (0, 4), (u32::MAX, u32::MAX)] {
        assert_eq!(
            step_advanced(
                AdvancedState::default(),
                AdvancedFrame {
                    group,
                    stance,
                    ..frame(0.01)
                },
                &o
            )
            .initialized,
            0
        );
    }
}
#[test]
fn pointer_validation_and_dormant_restore_values_are_safe() {
    let mut o = options();
    let mut output = o;
    let mut f = frame(0.1);
    f.native.position = [1.0, 2.0, 3.0];
    let initial = AdvancedState::default();
    let mut s = initial;
    unsafe {
        assert_eq!(cc_advanced_defaults(&mut output), 0);
        assert_eq!(output, ThirdOptions::default());
        assert_eq!(cc_advanced_defaults(std::ptr::null_mut()), 1);
        assert_eq!(cc_advanced_validate(std::ptr::null()), 1);
        assert_eq!(cc_advanced_validate(&o), 0);
        let bad_alignment = (&o as *const ThirdOptions)
            .cast::<u8>()
            .add(1)
            .cast::<ThirdOptions>();
        assert_eq!(cc_advanced_validate(bad_alignment), 1);
        o.profiles[55].world.min_rate = f32::NAN;
        assert_eq!(cc_advanced_validate(&o), 3);
        // The current frame only consumes profile zero; dormant data is checked on Apply.
        assert_eq!(cc_step_advanced(&initial, &f, &o, &mut s), 0);
        o.profiles[0].world.min_rate = f32::NAN;
        let previous = s;
        assert_eq!(cc_step_advanced(&initial, &f, &o, &mut s), 3);
        assert_eq!(s, previous);
        o = options();
        o.enabled = 0;
        assert_eq!(cc_step_advanced(&initial, &f, &o, &mut s), 0);
        assert_eq!(s.initialized, 0);
        assert_eq!(s.native, f.native.position);
    }
}

#[test]
fn pitch_zoom_only_applies_downward_and_clamps_at_configured_angle() {
    let mut o = options();
    o.pitch_enabled = 1;
    o.pitch_after = 1;
    o.pitch_curve = 0;
    o.pitch_max_angle = 45.0;
    o.pitch_zoom = 60.0;
    for (pitch, y) in [
        (-90.0, -100.0),
        (-45.0, -100.0),
        (0.0, -100.0),
        (22.5, -70.0),
        (45.0, -40.0),
        (90.0, -40.0),
    ] {
        let mut f = frame(0.01);
        f.pitch_degrees = pitch;
        assert_eq!(
            step_advanced(AdvancedState::default(), f, &o).position[1],
            y
        );
    }
}

#[test]
fn transition_endpoints_are_exact_even_after_cancellation() {
    let mut o = options();
    o.offset_duration = 0.1;
    o.zoom_duration = 0.1;
    o.fov_duration = 0.1;
    o.profiles[0].offset = [999.99, -999.99, 999.99];
    o.profiles[0].fov = 59.996;
    o.profiles[4].offset = [0.123456, -0.123456, 0.123456];
    o.profiles[4].fov = 0.001;
    let mut f = frame(0.1);
    let initial = step_advanced(AdvancedState::default(), f, &o);
    f.group = 1;
    let s = step_advanced(initial, f, &o);
    assert_eq!(s.offset, o.profiles[4].offset);
    assert_eq!(s.fov_delta, s.fov_target);
}

#[test]
fn reset_and_teleport_snap_frozen_filters_but_small_moves_do_not() {
    let mut o = options();
    let frozen = Follow {
        enabled: 1,
        min_rate: 0.0,
        max_rate: 0.0,
        ..Follow::default()
    };
    o.profiles[0].world = frozen;
    o.profiles[0].local = frozen;
    o.profiles[0].vertical = frozen;
    let initial = step_advanced(AdvancedState::default(), frame(0.01), &o);
    let mut f = frame(0.01);
    f.focus = [10.0, 0.0, 10.0];
    f.native.position = [10.0, -100.0, 10.0];
    assert_eq!(step_advanced(initial, f, &o).position, initial.position);
    f.native.reset = 1;
    assert_eq!(step_advanced(initial, f, &o).position, f.native.position);
    f.native.reset = 0;
    f.native.dt = 0.26;
    assert_eq!(step_advanced(initial, f, &o).position, f.native.position);
    f.native.dt = 0.01;
    f.focus[0] = 2000.0;
    f.native.position[0] = 2000.0;
    assert_eq!(step_advanced(initial, f, &o).position, f.native.position);
    f.focus = [0.0; 3]; // Native camera alone can teleport, too.
    assert_eq!(step_advanced(initial, f, &o).position, f.native.position);
    for field in 0..5 {
        let mut bad = initial;
        match field {
            0 => bad.orbit[0] = f32::NAN,
            1 => bad.elapsed[0] = f32::INFINITY,
            2 => bad.offset_from[0] = f32::NEG_INFINITY,
            3 => bad.fov_target = 100.0,
            _ => bad.focus[0] = f32::NAN,
        }
        let result = step_advanced(bad, frame(0.01), &o);
        assert_eq!(result.position, initial.position);
        assert_eq!(result.initialized, 1);
    }
}

#[test]
fn each_clamp_bit_limits_only_its_camera_axis_and_prevents_lag_accumulation() {
    let mut o = options();
    let frozen = Follow {
        enabled: 1,
        min_rate: 0.0,
        max_rate: 0.0,
        ..Follow::default()
    };
    o.profiles[0].world = frozen;
    o.profiles[0].vertical = frozen;
    o.profiles[0].local = frozen;
    o.clamp_min = [-2.0, -3.0, -4.0];
    o.clamp_max = [5.0, 6.0, 7.0];
    let mut f = frame(0.01);
    let initial = step_advanced(AdvancedState::default(), f, &o);
    f.focus = [100.0, -100.0, 100.0];
    f.native.position = [100.0, -200.0, 100.0];
    for (mask, expected) in [
        (1, [98.0, -100.0, 0.0]),
        (2, [0.0, -194.0, 0.0]),
        (4, [0.0, -100.0, 96.0]),
        (7, [98.0, -194.0, 96.0]),
    ] {
        o.clamp_mask = mask;
        let mut s = initial;
        for _ in 0..100 {
            s = step_advanced(s, f, &o);
            assert_eq!(s.position, expected);
        }
        assert_eq!(s.anchor, [0.0; 3]);
    }
}

#[test]
fn nonfinite_and_out_of_bound_inputs_cannot_publish_camera_results() {
    let o = options();
    let initial = step_advanced(AdvancedState::default(), frame(0.01), &o);
    for value in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY] {
        for field in 0..8 {
            let mut f = frame(0.01);
            match field {
                0 => f.native.position[0] = value,
                1 => f.focus[0] = value,
                2 => f.native.rotation[0] = value,
                3 => f.native.world_fov = value,
                4 => f.native.dt = value,
                5 => f.pitch_degrees = value,
                6 => f.native_side = value,
                _ => f.native_up = value,
            }
            let s = step_advanced(initial, f, &o);
            assert_eq!(s.initialized, 0);
            assert!(s.position.iter().all(|x| x.is_finite()));
        }
    }
    for field in 0..6 {
        let mut bad = o;
        match field {
            0 => bad.clamp_mask = 8,
            1 => bad.pitch_curve = 22,
            2 => bad.zoom_duration = -1.0,
            3 => bad.profiles[0].world.min_rate = 1.1,
            4 => bad.profiles[0].offset[0] = 1001.0,
            _ => bad.pitch_max_angle = 0.0,
        }
        assert!(!bad.valid());
        assert_eq!(step_advanced(initial, frame(0.01), &bad).initialized, 0);
    }
}

#[test]
fn older_advanced_settings_keep_native_geometry_and_new_fields_are_validated() {
    let before = options();
    let mut value = serde_json::to_value(before).unwrap();
    for key in ["preset_geometry", "min_distance", "zoom_scale"] {
        value.as_object_mut().unwrap().remove(key);
    }
    let restored: ThirdOptions = serde_json::from_value(value).unwrap();
    assert_eq!(before, restored);
    for (mode, distance, scale) in [(2, 250.0, 1.0), (1, -1.0, 1.0), (1, 250.0, f32::NAN)] {
        let bad = ThirdOptions {
            preset_geometry: mode,
            min_distance: distance,
            zoom_scale: scale,
            ..before
        };
        assert!(!bad.valid());
    }
    let bad = AdvancedFrame {
        zoom: f32::NAN,
        ..frame(0.016)
    };
    assert_eq!(
        step_advanced(AdvancedState::default(), bad, &before).initialized,
        0
    );
}
