use colony_camera::{align_body, BodyAlignmentFrame, BodyAlignmentOptions, BodyAlignmentResult};

fn frame() -> BodyAlignmentFrame {
    BodyAlignmentFrame {
        camera: [0.0, 0.0, 130.0],
        head: [0.0, 0.0, 125.0],
        heading: [0.0, 1.0],
        scale: 1.0,
        eye: [0.0; 3],
        eye_available: 0,
        body_root: [0.0; 3],
    }
}

#[test]
fn eye_and_head_select_height_diagnostics_without_reanchoring_the_body() {
    let input = BodyAlignmentFrame {
        head: [0.0, 0.0, 125.0],
        eye: [2.0, 5.0, 129.0],
        eye_available: 1,
        ..frame()
    };
    let options = BodyAlignmentOptions::default();
    let actual_eye = align_body(input, options);
    assert_eq!(actual_eye.valid, 1);
    close(actual_eye.translation, [0.0, -12.0, 0.0]);
    assert_eq!(actual_eye.vertical_error, 1.0);
    let fallback = align_body(
        BodyAlignmentFrame {
            eye_available: 0,
            ..input
        },
        options,
    );
    close(fallback.translation, [0.0, -12.0, 0.0]);
    assert_eq!(fallback.vertical_error, 5.0);
}

#[test]
fn absent_eye_storage_is_ignored_but_invalid_available_landmarks_fail_closed() {
    let options = BodyAlignmentOptions::default();
    let native_fallback = align_body(frame(), options);
    for eye in [
        [f32::NAN, 0.0, 125.0],
        [0.0, 0.0, f32::INFINITY],
        [0.0, 0.0, 1.1e8],
    ] {
        let absent = BodyAlignmentFrame {
            eye,
            eye_available: 0,
            ..frame()
        };
        assert_eq!(align_body(absent, options), native_fallback);
        assert_eq!(
            align_body(
                BodyAlignmentFrame {
                    eye_available: 1,
                    ..absent
                },
                options
            ),
            BodyAlignmentResult::default()
        );
    }
    assert_eq!(
        align_body(
            BodyAlignmentFrame {
                eye_available: 2,
                ..frame()
            },
            options
        ),
        BodyAlignmentResult::default()
    );
}

#[test]
fn finite_landmark_xy_never_controls_the_root_separation_guard() {
    let options = BodyAlignmentOptions::default();
    let input = BodyAlignmentFrame {
        eye: [1000.0, -1000.0, 129.0],
        eye_available: 1,
        head: [-2000.0, 3000.0, 125.0],
        ..frame()
    };
    let result = align_body(input, options);
    assert_eq!(result.valid, 1);
    close(result.translation, [0.0, -12.0, 0.0]);
    assert_eq!(result.vertical_error, 1.0);
    // Landmarks close to the camera cannot rescue a stale/mismatched root.
    let wrong_root = BodyAlignmentFrame {
        body_root: [80.01, 0.0, 0.0],
        ..frame()
    };
    assert_eq!(
        align_body(wrong_root, options),
        BodyAlignmentResult::default()
    );
}

#[test]
fn selected_eye_height_only_changes_the_reported_vertical_gap() {
    let input = BodyAlignmentFrame {
        eye: [0.0, 2.0, 129.0],
        eye_available: 1,
        ..frame()
    };
    let options = BodyAlignmentOptions::default();
    let baseline = align_body(input, options);
    for eye_z in [100.0, 130.0, 145.0] {
        let result = align_body(
            BodyAlignmentFrame {
                eye: [0.0, 2.0, eye_z],
                ..input
            },
            options,
        );
        assert_eq!(result.valid, 1);
        assert_eq!(result.translation, baseline.translation);
        assert_eq!(result.translation[2], 0.0);
        assert_eq!(result.vertical_error, 130.0 - eye_z);
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
fn captured_lateral_eye_offset_cannot_displace_the_body_root() {
    // Synthetic reconstruction of the reported bad pose: equal body/view yaw,
    // scale 1.03, eye local (17.21, 28.07, 95.47). Backset 12 is the tested default,
    // not a claim that the user's unknown settings had that value.
    let yaw = (-5.7_f32).to_radians();
    let forward = [yaw.sin(), yaw.cos()];
    let right = [forward[1], -forward[0]];
    let scale = 1.03;
    let root = [100.0, 200.0, 0.0];
    let eye = [
        root[0] + scale * (17.21 * right[0] + 28.07 * forward[0]),
        root[1] + scale * (17.21 * right[1] + 28.07 * forward[1]),
        scale * 95.47,
    ];
    let input = BodyAlignmentFrame {
        camera: [root[0], root[1], eye[2] + 0.3],
        body_root: root,
        eye,
        eye_available: 1,
        heading: forward,
        scale,
        ..frame()
    };
    let options = BodyAlignmentOptions::default();
    let baseline = align_body(input, options);
    assert_eq!(baseline.valid, 1);
    close(
        [
            baseline.translation[0] * right[0] + baseline.translation[1] * right[1],
            baseline.translation[0] * forward[0] + baseline.translation[1] * forward[1],
            baseline.translation[2],
        ],
        [0.0, -12.36, 0.0],
    );
    // A head-look animation or an equipment rebuild may move its landmarks in
    // either horizontal direction. Neither may move the entire body with it.
    for (head_xy, eye_xy) in [
        ([100.0, 200.0], [100.0, 200.0]),
        ([80.0, 220.0], [70.0, 230.0]),
        ([125.0, 180.0], [140.0, 175.0]),
    ] {
        let changed = BodyAlignmentFrame {
            head: [head_xy[0], head_xy[1], input.head[2]],
            eye: [eye_xy[0], eye_xy[1], eye[2]],
            ..input
        };
        assert_eq!(align_body(changed, options), baseline);
    }
}

#[test]
fn centered_torso_stays_centered_through_turns_despite_independent_head_motion() {
    // This fixture turns body and view together. It does not assert that the
    // helper corrects body yaw or a torso animation that itself leans sideways.
    let root = [10.0, 20.0, 0.0];
    let scale = 1.03;
    for yaw_degrees in [-180.0_f32, -90.0, -5.7, 0.0, 90.0, 180.0] {
        let yaw = yaw_degrees.to_radians();
        let forward = [yaw.sin(), yaw.cos()];
        let right = [forward[1], -forward[0]];
        let camera = [root[0] + 3.0, root[1] - 2.0, 100.0];
        let torso = [
            root[0] + scale * 9.0 * forward[0],
            root[1] + scale * 9.0 * forward[1],
            55.0 * scale,
        ];
        for eye_local in [[17.21, 28.07], [-20.0, 12.0], [0.0, 35.0]] {
            let input = BodyAlignmentFrame {
                camera,
                body_root: root,
                heading: forward,
                scale,
                eye: [
                    root[0] + scale * (eye_local[0] * right[0] + eye_local[1] * forward[0]),
                    root[1] + scale * (eye_local[0] * right[1] + eye_local[1] * forward[1]),
                    98.0,
                ],
                eye_available: 1,
                ..frame()
            };
            let result = align_body(input, BodyAlignmentOptions::default());
            assert_eq!(result.valid, 1);
            let torso_to_camera = [
                torso[0] + result.translation[0] - camera[0],
                torso[1] + result.translation[1] - camera[1],
            ];
            close(
                [
                    torso_to_camera[0] * right[0] + torso_to_camera[1] * right[1],
                    torso_to_camera[0] * forward[0] + torso_to_camera[1] * forward[1],
                    result.translation[2],
                ],
                [0.0, -3.0 * scale, 0.0],
            );
        }
    }
}

#[test]
fn fresh_roots_after_equipment_changes_and_resume_do_not_reuse_landmark_offsets() {
    // Value-only frames model the math inputs after a menu/equipment change.
    // Engine cache invalidation, menu gating and restore ownership need separate
    // bridge tests; this helper has no persistent state or skeleton identity.
    let options = BodyAlignmentOptions::default();
    for (root, head, eye, eye_available) in [
        ([0.0, 0.0, 0.0], [0.0, 5.0, 125.0], [17.0, 28.0, 129.0], 1),
        ([30.0, 20.0, 0.0], [20.0, 32.0, 125.0], [f32::NAN; 3], 0),
        (
            [30.0, 20.0, 0.0],
            [42.0, 25.0, 125.0],
            [52.0, 50.0, 129.0],
            1,
        ),
    ] {
        let input = BodyAlignmentFrame {
            camera: [root[0] + 2.0, root[1] + 3.0, 130.0],
            body_root: root,
            head,
            eye,
            eye_available,
            ..frame()
        };
        let applied = align_body(input, options);
        assert_eq!(applied.valid, 1);
        close(applied.translation, [2.0, -9.0, 0.0]);
        let disabled = align_body(
            input,
            BodyAlignmentOptions {
                alignment_enabled: 0,
                ..options
            },
        );
        assert_eq!(disabled.valid, 1);
        assert_eq!(disabled.translation, [0.0; 3]);
        assert_eq!(align_body(input, options), applied);
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
        body_root: [8.0, 17.0, 0.0],
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
        body_root: [408.0, -483.0, 200.0],
        ..input
    };
    assert_eq!(align_body(moved, options), expected);
    // The bridge applies the resulting world displacement using the actual
    // parent inverse; local parent scale is not supplied or multiplied here.
}

#[test]
fn strafing_and_rapid_turns_align_to_the_final_camera_without_filter_lag() {
    let options = BodyAlignmentOptions {
        body_side: 4.0,
        ..BodyAlignmentOptions::default()
    };
    // Synthetic native model and final camera positions differ as they can after
    // camera damping/collision. The bridge must pass the final camera position;
    // this tests the math contract, not the engine callback's sampling order.
    let sequence = [
        (
            [100.0, 200.0, 130.0],
            [100.0, 200.0, 130.0],
            [100.0, 204.0, 128.0],
            [0.0, 1.0],
            [4.0, -12.0],
        ),
        (
            [124.0, 200.0, 132.0],
            [108.0, 200.0, 130.0],
            [122.0, 204.0, 130.0],
            [0.0, 1.0],
            [4.0, -12.0],
        ),
        (
            [148.0, 210.0, 128.0],
            [119.0, 203.0, 129.0],
            [144.0, 214.0, 127.0],
            [1.0, 0.0],
            [-12.0, -4.0],
        ),
        (
            [130.0, 240.0, 132.0],
            [125.0, 213.0, 131.0],
            [126.0, 235.0, 129.0],
            [0.0, -1.0],
            [-4.0, 12.0],
        ),
        (
            [100.0, 248.0, 130.0],
            [110.0, 225.0, 130.0],
            [96.0, 242.0, 128.0],
            [-1.0, 0.0],
            [12.0, 4.0],
        ),
    ];
    for (raw_model, published_camera, eye, heading, expected_offset) in sequence {
        let input = BodyAlignmentFrame {
            camera: published_camera,
            body_root: [raw_model[0], raw_model[1], 0.0],
            eye,
            eye_available: 1,
            head: [eye[0] - 2.0, eye[1] - 3.0, eye[2] - 4.0],
            heading,
            ..frame()
        };
        let result = align_body(input, options);
        assert_eq!(result.valid, 1);
        close(
            [
                input.body_root[0] + result.translation[0] - published_camera[0],
                input.body_root[1] + result.translation[1] - published_camera[1],
                result.translation[2],
            ],
            [expected_offset[0], expected_offset[1], 0.0],
        );
        // Sampling the raw model point would leave its difference from the
        // displayed camera visible in full; the pure helper cannot remove it.
        let wrong_sample = align_body(
            BodyAlignmentFrame {
                camera: raw_model,
                ..input
            },
            options,
        );
        assert_eq!(wrong_sample.valid, 1);
        close(
            [
                wrong_sample.translation[0] - result.translation[0],
                wrong_sample.translation[1] - result.translation[1],
                wrong_sample.translation[2],
            ],
            [
                raw_model[0] - published_camera[0],
                raw_model[1] - published_camera[1],
                0.0,
            ],
        );
    }
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
        changed.body_root[2] = head_z - 100.0;
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
    assert_eq!(
        align_body(
            BodyAlignmentFrame {
                body_root: [81.0, 0.0, 0.0],
                ..frame()
            },
            options
        ),
        BodyAlignmentResult::default()
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
            body_root: [f32::NAN, 0.0, 0.0],
            ..frame()
        },
        BodyAlignmentFrame {
            body_root: [0.0, f32::NEG_INFINITY, 0.0],
            ..frame()
        },
        BodyAlignmentFrame {
            body_root: [0.0, 0.0, 1.1e8],
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
