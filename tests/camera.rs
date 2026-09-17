use colony_camera::{step, Frame, Profile, State};
fn frame(x: f32, dt: f32) -> Frame {
    Frame {
        position: [x, 0.0, 0.0],
        rotation: [1.0, 0.0, 0.0, 0.0],
        dt,
        reset: 0,
        world_fov: 75.0,
    }
}
#[test]
fn converges_without_overshoot_at_equal_elapsed_time() {
    let profile = Profile {
        half_life: 0.1,
        max_lag: 300.0,
        ..Profile::default()
    };
    let run = |fps| {
        let mut s = step(State::default(), frame(0.0, 0.01), profile);
        for _ in 0..fps {
            s = step(s, frame(100.0, 1.0 / fps as f32), profile);
        }
        s.position[0]
    };
    assert!((run(30) - run(144)).abs() < 0.001);
    assert!((run(60) - 99.90234).abs() < 0.001);
}
#[test]
fn resets_teleports_and_bounds_camera_lag() {
    let p = Profile {
        max_lag: 20.0,
        ..Profile::default()
    };
    let s = step(State::default(), frame(0.0, 0.01), p);
    let next = step(s, frame(100.0, 0.01), p);
    assert!(next.position[0] >= 80.0);
    assert_eq!(step(next, frame(5000.0, 0.01), p).position[0], 5000.0);
    let mut f = frame(10.0, 0.01);
    f.reset = 1;
    assert_eq!(step(next, f, p).position[0], 10.0);
}
#[test]
fn invalid_inputs_never_poison_state() {
    let p = Profile::default();
    let s = step(State::default(), frame(10.0, 0.01), p);
    assert_eq!(step(s, frame(f32::NAN, 0.01), p).position, s.position);
    assert_eq!(step(s, frame(f32::NAN, 0.01), p).initialized, 0);
    assert_eq!(step(s, frame(20.0, f32::NAN), p).position, s.position);
    assert_eq!(step(s, frame(20.0, -1.0), p).position, s.position);
}
#[test]
fn rotates_offsets_and_snaps_after_long_pause() {
    let p = Profile {
        offset: [10.0, 0.0, 0.0],
        ..Profile::default()
    };
    let mut f = frame(0.0, 0.01);
    let h = std::f32::consts::FRAC_1_SQRT_2;
    f.rotation = [h, 0.0, 0.0, h];
    let s = step(State::default(), f, p);
    assert!(s.position[0].abs() < 0.001);
    assert!((s.position[1] - 10.0).abs() < 0.001);
    let f = frame(100.0, 2.0);
    assert_eq!(step(s, f, Profile::default()).position[0], 100.0);
}

#[test]
fn each_transition_has_its_own_half_life() {
    let initial = step(State::default(), frame(0.0, 0.01), Profile::default());
    let profile = Profile {
        offset: [20.0, 0.0, 0.0],
        half_life: 0.10,
        max_lag: 300.0,
        offset_half_life: 0.20,
        zoom: 40.0,
        zoom_half_life: 0.05,
        fov_offset: 20.0,
        fov_half_life: 0.10,
    };
    let result = step(initial, frame(100.0, 0.10), profile);
    assert!((result.base[0] - 50.0).abs() < 0.001);
    assert!((result.offset[0] - 20.0 * (1.0 - 0.5f32.sqrt())).abs() < 0.001);
    assert!((result.zoom - 30.0).abs() < 0.001);
    assert!((result.fov - 85.0).abs() < 0.001);
    assert!((result.position[0] - result.base[0] - result.offset[0]).abs() < 0.001);
    assert_eq!(result.position[1], result.zoom);
}

#[test]
fn aim_defaults_remove_all_previous_transform_and_fov_lag() {
    let profile = Profile {
        offset: [30.0, 10.0, 5.0],
        zoom: 50.0,
        fov_offset: 20.0,
        ..Profile::default()
    };
    let previous = step(State::default(), frame(0.0, 0.01), profile);
    let aim = colony_camera::Config::default().profiles[colony_camera::AIM as usize];
    let result = step(previous, frame(100.0, 0.001), aim);
    assert_eq!(result.position, [100.0, 0.0, 0.0]);
    assert_eq!(result.fov, 75.0);
}

#[test]
fn collision_feedback_does_not_accumulate_scene_displacement() {
    let profile = Profile {
        half_life: 0.1,
        max_lag: 100.0,
        ..Profile::default()
    };
    let mut state = step(State::default(), frame(0.0, 0.01), profile);
    for _ in 0..1000 {
        let mut candidate = step(state, frame(100.0, 1.0 / 60.0), profile);
        let collision_x = candidate.position[0].min(20.0);
        candidate.base[0] += collision_x - candidate.position[0];
        candidate.position[0] = collision_x;
        state = candidate;
        assert!(state.position[0] <= 20.0);
        assert!(state.base[0] >= 0.0);
    }
    assert!((state.position[0] - 20.0).abs() < 0.001);
    let recovered = step(state, frame(100.0, 0.1), profile);
    assert!((recovered.position[0] - 60.0).abs() < 0.001);
}

#[test]
fn bad_history_recovers_and_all_invalid_extended_inputs_fail_closed() {
    let good = step(State::default(), frame(0.0, 0.01), Profile::default());
    let mut corrupt = good;
    corrupt.base[0] = f32::NAN;
    assert_eq!(
        step(corrupt, frame(10.0, 0.01), Profile::default()).position[0],
        10.0
    );
    let mut f = frame(10.0, 0.01);
    f.world_fov = f32::INFINITY;
    assert_eq!(step(good, f, Profile::default()).initialized, 0);
    for p in [
        Profile {
            zoom: f32::NAN,
            ..Profile::default()
        },
        Profile {
            fov_offset: 61.0,
            ..Profile::default()
        },
        Profile {
            offset_half_life: -0.1,
            ..Profile::default()
        },
        Profile {
            zoom_half_life: 2.0,
            ..Profile::default()
        },
        Profile {
            fov_half_life: f32::INFINITY,
            ..Profile::default()
        },
    ] {
        assert_eq!(step(good, frame(10.0, 0.01), p).initialized, 0);
    }
}

#[test]
fn offset_zoom_and_fov_converge_at_equal_elapsed_time() {
    let start = Profile::default();
    let target = Profile {
        offset: [100.0, 40.0, -20.0],
        zoom: 100.0,
        fov_offset: 20.0,
        offset_half_life: 0.2,
        zoom_half_life: 0.1,
        fov_half_life: 0.3,
        ..start
    };
    let run = |fps| {
        let mut state = step(State::default(), frame(0.0, 0.0), start);
        for _ in 0..fps {
            state = step(state, frame(0.0, 1.0 / fps as f32), target);
        }
        state
    };
    let (a, b) = (run(30), run(144));
    assert!((a.offset[0] - b.offset[0]).abs() < 0.001);
    assert!((a.zoom - b.zoom).abs() < 0.001);
    assert!((a.fov - b.fov).abs() < 0.001);
}

#[test]
fn native_fov_changes_are_immediate_while_only_our_adjustment_blends() {
    let mut frame = Frame {
        world_fov: 75.0,
        dt: 0.1,
        ..Frame::default()
    };
    let state = step(State::default(), frame, Profile::default());
    frame.world_fov = 55.0;
    let native = step(state, frame, Profile::default());
    assert_eq!(native.fov, 55.0);
    assert_eq!(native.fov_delta, 0.0);
    let profile = Profile {
        fov_offset: 20.0,
        fov_half_life: 0.1,
        ..Profile::default()
    };
    let adjusted = step(native, frame, profile);
    assert_eq!(adjusted.fov_delta, 10.0);
    assert_eq!(adjusted.fov, 65.0);
    frame.world_fov = 90.0;
    let changed = step(adjusted, frame, profile);
    assert_eq!(changed.fov_delta, 15.0);
    assert_eq!(changed.fov, 105.0);
}

#[test]
fn fov_bounds_apply_to_composed_result_and_preserve_unadjusted_native_fov() {
    let f = Frame {
        world_fov: 170.0,
        ..Frame::default()
    };
    assert_eq!(step(State::default(), f, Profile::default()).fov, 170.0);
    let profile = Profile {
        fov_offset: 60.0,
        ..Profile::default()
    };
    assert_eq!(step(State::default(), f, profile).fov, 150.0);
    let f = Frame {
        world_fov: 20.0,
        ..Frame::default()
    };
    let profile = Profile {
        fov_offset: -60.0,
        ..Profile::default()
    };
    assert_eq!(step(State::default(), f, profile).fov, 30.0);
}

#[test]
fn adjusted_fov_can_blend_back_to_native_values_outside_the_target_bounds() {
    let frame = Frame {
        world_fov: 170.0,
        dt: 0.1,
        ..Frame::default()
    };
    let profile = Profile {
        fov_offset: 60.0,
        fov_half_life: 0.1,
        ..Profile::default()
    };
    let adjusted = step(State::default(), frame, profile);
    assert_eq!(adjusted.fov, 150.0);
    let native = Profile {
        fov_offset: 0.0,
        ..profile
    };
    let returning = step(adjusted, frame, native);
    assert_eq!(returning.fov, 160.0);
    assert_eq!(step(returning, frame, native).fov, 165.0);
}

#[test]
fn boundary_fov_delta_rounding_does_not_reset_position_history() {
    for (native_fov, adjustment) in [(75.001, 60.0), (135.001, -60.0)] {
        let mut frame = Frame {
            world_fov: native_fov,
            dt: 0.1,
            ..Frame::default()
        };
        let profile = Profile {
            half_life: 0.1,
            max_lag: 300.0,
            fov_offset: adjustment,
            ..Profile::default()
        };
        let initial = step(State::default(), frame, profile);
        assert!((-60.0..=60.0).contains(&initial.fov_delta));
        frame.position[0] = 100.0;
        let next = step(initial, frame, profile);
        assert!((next.base[0] - 50.0).abs() < 0.001);
        assert!((-60.0..=60.0).contains(&next.fov_delta));
    }
}

#[test]
fn rapid_transitions_never_round_beyond_valid_endpoints() {
    for sign in [-1.0, 1.0] {
        let mut frame = Frame {
            world_fov: 90.0,
            dt: 0.1,
            ..Frame::default()
        };
        let initial_profile = Profile {
            offset: [-299.999 * sign, 0.0, 0.0],
            zoom: -299.999 * sign,
            fov_offset: -59.996 * sign,
            ..Profile::default()
        };
        let initial = step(State::default(), frame, initial_profile);
        let target = Profile {
            offset: [300.0 * sign, 0.0, 0.0],
            zoom: 300.0 * sign,
            fov_offset: 60.0 * sign,
            half_life: 0.1,
            max_lag: 300.0,
            offset_half_life: 0.000001,
            zoom_half_life: 0.000001,
            fov_half_life: 0.000001,
        };
        frame.position[0] = 100.0;
        let changed = step(initial, frame, target);
        assert_eq!(changed.offset[0], 300.0 * sign);
        assert_eq!(changed.zoom, 300.0 * sign);
        assert_eq!(changed.fov_delta, 60.0 * sign);
        assert!((changed.base[0] - 50.0).abs() < 0.001);
        let next = step(changed, frame, target);
        assert!((next.base[0] - 75.0).abs() < 0.001);
    }
}

#[test]
fn composed_fov_remains_valid_after_extreme_native_changes() {
    for (native_start, native_next, adjustment, expected) in
        [(1.0, 179.0, 60.0, 179.0), (179.0, 1.0, -60.0, 1.0)]
    {
        let profile = Profile {
            fov_offset: adjustment,
            half_life: 0.1,
            max_lag: 300.0,
            fov_half_life: 0.1,
            ..Profile::default()
        };
        let mut frame = Frame {
            world_fov: native_start,
            ..Frame::default()
        };
        let initial = step(State::default(), frame, profile);
        frame.world_fov = native_next;
        frame.position[0] = 100.0;
        let changed = step(initial, frame, profile);
        assert_eq!(changed.fov, expected);
        assert_eq!(changed.base[0], 0.0);
        frame.dt = 0.1;
        frame.world_fov = 75.0;
        let recovered = step(changed, frame, profile);
        assert!((recovered.base[0] - 50.0).abs() < 0.001);
        assert!((1.0..=179.0).contains(&recovered.fov));
        assert!((-60.0..=60.0).contains(&recovered.fov_delta));
    }
}
