// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Placement adapted from ImprovedCameraSE-NG AdjustModelPosition(false) and
// TranslateThirdPersonModel, ArranzCNL and contributors, revision
// 2e441c190e46d96eefb7738a3276308e9c36e939. Modified 2026-09-19:
// Rust value ABI, validated inputs, parent-space conversion in C++, bounded output.
// Additionally available under GPL-3.0-or-later in this combined work under MPL 3.3.
//! No-headbob reference placement. This math never moves a camera, changes FOV,
//! moves native first-person arms, or alters a skeleton's scale or rotation.

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct BodyAlignmentFrame {
    pub camera: [f32; 3],
    pub head: [f32; 3],
    /// Horizontal rendered-view heading, for diagnostics; placement uses body_rotation.
    pub heading: [f32; 2],
    /// Unsuppressed cumulative skeleton world scale, not a local bone scale.
    pub scale: f32,
    /// Actual unsuppressed third-person eye landmark used by reference placement.
    pub eye: [f32; 3],
    /// Must be one for reference placement; missing eyes use native fallback.
    pub eye_available: u32,
    /// Required unmodified third-person locomotion-root world position. Sample
    /// after restoring the previous body lease, before applying this correction.
    pub body_root: [f32; 3],
    /// Row-major native body rotation, not camera rotation.
    pub body_rotation: [f32; 9],
    /// Bits: sneak=1, right=2, left=4, forward=8, back=16.
    pub movement: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodyAlignmentOptions {
    pub alignment_enabled: u32,
    /// Additional backward offset in body axes, at scale one; reference default 8.
    pub body_backset: f32,
    /// Positive moves the body to body-right, in skeleton units at scale one.
    pub body_side: f32,
}
impl Default for BodyAlignmentOptions {
    fn default() -> Self {
        // Dormant until the separately gated first-person experiment is enabled.
        Self {
            alignment_enabled: 1,
            body_backset: 8.0,
            body_side: 0.0,
        }
    }
}
impl BodyAlignmentOptions {
    pub fn valid(&self) -> bool {
        self.alignment_enabled <= 1
            && self.body_backset.is_finite()
            && (0.0..=40.0).contains(&self.body_backset)
            && self.body_side.is_finite()
            && (-20.0..=20.0).contains(&self.body_side)
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct BodyAlignmentResult {
    /// World-space displacement for a leased third-person root translation.
    /// The native root must be restored before each input frame is sampled.
    pub translation: [f32; 3],
    /// Camera Z minus eye landmark Z; diagnostics only, never applied.
    pub vertical_error: f32,
    pub valid: u32,
}

pub fn align_body(frame: BodyAlignmentFrame, options: BodyAlignmentOptions) -> BodyAlignmentResult {
    let invalid = BodyAlignmentResult::default();
    if !options.valid()
        || !frame
            .camera
            .iter()
            .chain(&frame.head)
            .chain(&frame.body_root)
            .all(|v| v.is_finite() && v.abs() <= 1e8)
        || !frame.heading.iter().all(|v| v.is_finite())
        || !frame.scale.is_finite()
        || !(0.1..=10.0).contains(&frame.scale)
        || frame.eye_available > 1
        || (frame.eye_available == 1 && !frame.eye.iter().all(|v| v.is_finite() && v.abs() <= 1e8))
    {
        return invalid;
    }
    if frame.eye_available != 1 || frame.movement & !31 != 0 {
        return invalid;
    }
    let r = frame.body_rotation;
    if !r.iter().all(|x| x.is_finite()) {
        return invalid;
    }
    for i in 0..3 {
        for j in 0..3 {
            let dot: f32 = (0..3).map(|k| r[i * 3 + k] * r[j * 3 + k]).sum();
            if (dot - if i == j { 1.0 } else { 0.0 }).abs() > 0.001 {
                return invalid;
            }
        }
    }
    let determinant = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6])
        + r[2] * (r[3] * r[7] - r[4] * r[6]);
    if (determinant - 1.0).abs() > 0.001 {
        return invalid;
    }
    let distance = |a: [f32; 3], b: [f32; 3]| {
        ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2)).sqrt()
    };
    // Reference no-headbob policy uses magnitudes, not camera-minus-eye XY.
    let forward = distance(frame.head, frame.camera) + distance(frame.head, frame.eye);
    let mut lateral = frame.head[0] - frame.camera[0];
    let m = frame.movement;
    if m & 1 != 0 {
        lateral = 0.0;
    } else if m & 2 != 0 && m & (8 | 16) == 0 {
        lateral = lateral.abs();
    } else if m & 4 != 0 && m & 8 == 0 {
        lateral = -lateral.abs();
    } else {
        lateral = 0.0;
    }
    let mut result = BodyAlignmentResult {
        vertical_error: frame.camera[2] - frame.eye[2],
        valid: 1,
        ..invalid
    };
    if options.alignment_enabled == 0 {
        return result;
    }
    let local = [
        options.body_side * frame.scale - lateral,
        -options.body_backset * frame.scale - forward,
        0.0,
    ];
    for i in 0..3 {
        result.translation[i] = (0..3).map(|k| r[i * 3 + k] * local[k]).sum();
    }
    if !result.translation.iter().all(|x| x.is_finite())
        || result.translation.iter().map(|x| x * x).sum::<f32>() > 128.0 * 128.0
    {
        return invalid;
    }
    result
}

#[no_mangle]
pub extern "C" fn cc_align_body(
    frame: BodyAlignmentFrame,
    options: BodyAlignmentOptions,
) -> BodyAlignmentResult {
    align_body(frame, options)
}
