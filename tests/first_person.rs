use colony_camera::{align_body, BodyAlignmentFrame, BodyAlignmentOptions, BodyAlignmentResult};

fn frame() -> BodyAlignmentFrame {
    BodyAlignmentFrame {
        camera: [0.0, 0.0, 130.0],
        head: [0.0, 0.0, 125.0],
        heading: [0.0, 1.0],
        scale: 1.0,
    }
}
fn close(actual: [f32; 3], expected: [f32; 3]) {
    for i in 0..3 {
        assert!(
            (actual[i] - expected[i]).abs() < 0.0001,
            "{actual:?} != {expected:?}"
        );
    }
}

#[test]
fn backward_and_right_adjustments_follow_all_heading_quadrants() {
    let options = BodyAlignmentOptions {
        body_side: 4.0,
        ..BodyAlignmentOptions::default()
    };
    for (heading, expected) in [
        ([0.0, 1.0], [4.0, -12.0, 0.0]),
        ([1.0, 0.0], [-12.0, -4.0, 0.0]),
        ([0.0, -1.0], [-4.0, 12.0, 0.0]),
        ([-1.0, 0.0], [12.0, 4.0, 0.0]),
    ] {
        let result = align_body(BodyAlignmentFrame { heading, ..frame() }, options);
        assert_eq!(result.valid, 1);
        close(result.translation, expected);
        assert_eq!(result.vertical_error, 5.0);
    }
}

#[test]
fn heading_is_normalized_and_skeleton_scale_is_applied_once() {
    let options = BodyAlignmentOptions {
        body_side: 4.0,
        ..BodyAlignmentOptions::default()
    };
    for scale in [0.1, 0.5, 1.0, 2.0] {
        let result = align_body(
            BodyAlignmentFrame {
                heading: [0.0, 10.0],
                scale,
                ..frame()
            },
            options,
        );
        assert_eq!(result.valid, 1);
        close(result.translation, [4.0 * scale, -12.0 * scale, 0.0]);
        assert_eq!(result.vertical_error, 5.0);
    }
    let diagonal = align_body(
        BodyAlignmentFrame {
            heading: [3.0, 4.0],
            ..frame()
        },
        options,
    );
    close(diagonal.translation, [-4.0, -12.0, 0.0]);
}

#[test]
fn world_space_anchor_correction_does_not_depend_on_parent_translation() {
    let input = BodyAlignmentFrame {
        camera: [10.0, 20.0, 130.0],
        head: [8.0, 17.0, 125.0],
        ..frame()
    };
    let options = BodyAlignmentOptions {
        body_side: 4.0,
        ..BodyAlignmentOptions::default()
    };
    let expected = align_body(input, options);
    close(expected.translation, [6.0, -9.0, 0.0]);
    let moved = BodyAlignmentFrame {
        camera: [410.0, -480.0, 330.0],
        head: [408.0, -483.0, 325.0],
        ..input
    };
    assert_eq!(align_body(moved, options), expected);
    // The bridge applies the resulting world displacement using the actual
    // parent inverse; local parent scale is not supplied or multiplied here.
}

#[test]
fn vertical_changes_never_lift_the_feet_or_feed_a_pitch_rotation() {
    let input = frame();
    let options = BodyAlignmentOptions::default();
    let original = align_body(input, options);
    for (camera_z, head_z) in [(140.0, 90.0), (-30.0, 100.0), (1000.0, -500.0)] {
        let mut changed = input;
        changed.camera[2] = camera_z;
        changed.head[2] = head_z;
        let result = align_body(changed, options);
        assert_eq!(result.valid, 1);
        assert_eq!(result.translation, original.translation);
        assert_eq!(result.translation[2], 0.0);
        assert_eq!(result.vertical_error, camera_z - head_z);
    }
}

#[test]
fn native_inputs_produce_repeatable_displacement_without_accumulation() {
    let input = frame();
    let options = BodyAlignmentOptions::default();
    let expected = align_body(input, options);
    for _ in 0..1000 {
        assert_eq!(align_body(input, options), expected);
    }
}

#[test]
fn disabled_alignment_preserves_native_placement_but_still_validates_inputs() {
    let options = BodyAlignmentOptions {
        alignment_enabled: 0,
        ..BodyAlignmentOptions::default()
    };
    let result = align_body(frame(), options);
    assert_eq!(
        result,
        BodyAlignmentResult {
            translation: [0.0; 3],
            vertical_error: 5.0,
            valid: 1
        }
    );
    assert_eq!(
        align_body(
            BodyAlignmentFrame {
                scale: 0.0,
                ..frame()
            },
            options
        )
        .valid,
        0
    );
    assert_eq!(
        align_body(
            BodyAlignmentFrame {
                heading: [0.0; 2],
                ..frame()
            },
            options
        )
        .valid,
        0
    );
}

#[test]
fn invalid_numbers_scales_and_options_return_no_partial_correction() {
    for input in [
        BodyAlignmentFrame {
            camera: [f32::NAN, 0.0, 0.0],
            ..frame()
        },
        BodyAlignmentFrame {
            head: [0.0, f32::INFINITY, 0.0],
            ..frame()
        },
        BodyAlignmentFrame {
            camera: [0.0, 0.0, 1.1e8],
            ..frame()
        },
        BodyAlignmentFrame {
            heading: [f32::NAN, 1.0],
            ..frame()
        },
        BodyAlignmentFrame {
            heading: [f32::MAX; 2],
            ..frame()
        },
        BodyAlignmentFrame {
            heading: [0.0, 0.00001],
            ..frame()
        },
        BodyAlignmentFrame {
            scale: f32::NAN,
            ..frame()
        },
        BodyAlignmentFrame {
            scale: 0.099,
            ..frame()
        },
        BodyAlignmentFrame {
            scale: 10.01,
            ..frame()
        },
    ] {
        assert_eq!(
            align_body(input, BodyAlignmentOptions::default()),
            BodyAlignmentResult::default()
        );
    }
    for options in [
        BodyAlignmentOptions {
            alignment_enabled: 2,
            ..BodyAlignmentOptions::default()
        },
        BodyAlignmentOptions {
            body_backset: -0.01,
            ..BodyAlignmentOptions::default()
        },
        BodyAlignmentOptions {
            body_backset: 40.01,
            ..BodyAlignmentOptions::default()
        },
        BodyAlignmentOptions {
            body_backset: f32::NAN,
            ..BodyAlignmentOptions::default()
        },
        BodyAlignmentOptions {
            body_side: -20.01,
            ..BodyAlignmentOptions::default()
        },
        BodyAlignmentOptions {
            body_side: 20.01,
            ..BodyAlignmentOptions::default()
        },
    ] {
        assert_eq!(align_body(frame(), options), BodyAlignmentResult::default());
    }
}

#[test]
fn implausible_native_gaps_and_excessive_displacements_fall_back_without_saturation() {
    let options = BodyAlignmentOptions::default();
    let gap = BodyAlignmentFrame {
        camera: [0.0, 80.01, 130.0],
        ..frame()
    };
    assert_eq!(align_body(gap, options), BodyAlignmentResult::default());
    let scaled_gap = BodyAlignmentFrame {
        camera: [0.0, 8.01, 130.0],
        scale: 0.1,
        ..frame()
    };
    assert_eq!(align_body(scaled_gap, options).valid, 0);
    let allowed = BodyAlignmentFrame {
        camera: [0.0, -80.0, 130.0],
        ..frame()
    };
    let maximum = BodyAlignmentOptions {
        body_backset: 40.0,
        body_side: 20.0,
        ..options
    };
    let result = align_body(allowed, maximum);
    assert_eq!(result.valid, 1);
    close(result.translation, [20.0, -120.0, 0.0]);
    let excessive = BodyAlignmentFrame {
        camera: [0.0, -160.0, 130.0],
        scale: 2.0,
        ..frame()
    };
    assert_eq!(
        align_body(excessive, maximum),
        BodyAlignmentResult::default()
    );
    let large_scale = align_body(
        BodyAlignmentFrame {
            scale: 10.0,
            ..frame()
        },
        options,
    );
    assert_eq!(large_scale.valid, 1);
    close(large_scale.translation, [0.0, -120.0, 0.0]);
}
