use colony_camera::{step, Frame, Profile, State};
fn frame(x: f32, dt: f32) -> Frame {
    Frame {
        position: [x, 0.0, 0.0],
        rotation: [1.0, 0.0, 0.0, 0.0],
        dt,
        reset: 0,
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
