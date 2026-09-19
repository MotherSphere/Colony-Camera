use colony_camera::{align_body, BodyAlignmentFrame, BodyAlignmentOptions};
fn frame() -> BodyAlignmentFrame {
    BodyAlignmentFrame {
        camera: [0., 0., 130.],
        head: [0., 0., 125.],
        eye: [0., 3., 129.],
        eye_available: 1,
        heading: [0., 1.],
        scale: 1.,
        body_root: [0.; 3],
        body_rotation: [1., 0., 0., 0., 1., 0., 0., 0., 1.],
        movement: 0,
    }
}
fn close(a: [f32; 3], b: [f32; 3]) {
    for i in 0..3 {
        assert!((a[i] - b[i]).abs() < 0.0001, "{a:?} != {b:?}");
    }
}
#[test]
fn reference_no_headbob_uses_landmark_distances_and_body_axes() {
    // IC AdjustModelPosition(false): head-camera 5 + head-eye 5 + backset 8.
    let f = frame();
    let o = BodyAlignmentOptions::default();
    close(align_body(f, o).translation, [0., -18., 0.]);
    let turned = BodyAlignmentFrame {
        body_rotation: [0., -1., 0., 1., 0., 0., 0., 0., 1.],
        ..f
    };
    close(align_body(turned, o).translation, [18., 0., 0.]);
    // View heading does not drag the body around the camera in this policy.
    close(
        align_body(
            BodyAlignmentFrame {
                heading: [1., 0.],
                ..f
            },
            o,
        )
        .translation,
        [0., -18., 0.],
    );
}
#[test]
fn reference_strafe_rules_preserve_directional_precedence() {
    let f = BodyAlignmentFrame {
        head: [3., 0., 130.],
        eye: [3., 0., 130.],
        ..frame()
    };
    let o = BodyAlignmentOptions::default();
    for (movement, x) in [
        (0, 0.),
        (1, 0.),
        (2, -3.),
        (4, 3.),
        (2 | 8, 0.),
        (2 | 16, 0.),
        (4 | 16, 3.),
        (1 | 2, 0.),
    ] {
        let r = align_body(BodyAlignmentFrame { movement, ..f }, o);
        assert_eq!(r.valid, 1);
        close(r.translation, [x, -11., 0.]);
    }
}
#[test]
fn native_pose_is_not_reanchored_and_height_is_preserved_for_upright_body() {
    let f = frame();
    let o = BodyAlignmentOptions::default();
    let r = align_body(f, o);
    close(r.translation, [0., -18., 0.]);
    close(
        align_body(
            BodyAlignmentFrame {
                body_root: [5., 7., 0.],
                ..f
            },
            o,
        )
        .translation,
        r.translation,
    );
    close(
        align_body(
            BodyAlignmentFrame {
                camera: [100., 200., 430.],
                head: [100., 200., 425.],
                eye: [100., 203., 429.],
                body_root: [100., 200., 300.],
                ..f
            },
            o,
        )
        .translation,
        r.translation,
    );
    assert_eq!(
        align_body(
            f,
            BodyAlignmentOptions {
                alignment_enabled: 0,
                ..o
            }
        )
        .translation,
        [0.; 3]
    );
    for _ in 0..100 {
        assert_eq!(align_body(f, o), r);
    }
}
#[test]
fn invalid_missing_landmarks_and_nonrigid_bases_fall_back_atomically() {
    let f = frame();
    let o = BodyAlignmentOptions::default();
    for bad in [
        BodyAlignmentFrame {
            eye_available: 0,
            ..f
        },
        BodyAlignmentFrame { movement: 32, ..f },
        BodyAlignmentFrame {
            eye: [f32::NAN; 3],
            ..f
        },
        BodyAlignmentFrame { scale: 0., ..f },
        BodyAlignmentFrame {
            body_rotation: [0.; 9],
            ..f
        },
        BodyAlignmentFrame {
            body_rotation: [1., 0.5, 0., 0., 1., 0., 0., 0., 1.],
            ..f
        },
        BodyAlignmentFrame {
            camera: [1e9, 0., 0.],
            ..f
        },
    ] {
        let r = align_body(bad, o);
        assert_eq!(r.valid, 0);
        assert_eq!(r.translation, [0.; 3]);
    }
}

#[test]
fn reference_scaling_tilt_and_displacement_limits() {
    let mut f = frame();
    let o = BodyAlignmentOptions {
        body_side: 2.,
        ..BodyAlignmentOptions::default()
    };
    f.scale = 2.;
    close(align_body(f, o).translation, [4., -26., 0.]);
    f.body_rotation = [1., 0., 0., 0., 0., -1., 0., 1., 0.];
    close(align_body(f, o).translation, [4., 0., -26.]);
    // Finite values can still exceed safe placement limits.
    f.camera = [0., 0., 1000.];
    assert_eq!(align_body(f, o).valid, 0);
    for backset in [-1., 41., f32::NAN, f32::INFINITY] {
        assert_eq!(
            align_body(
                frame(),
                BodyAlignmentOptions {
                    body_backset: backset,
                    ..o
                }
            )
            .valid,
            0
        );
    }
}
