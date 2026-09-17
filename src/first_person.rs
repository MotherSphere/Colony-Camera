//! Horizontal body alignment only. This math never moves a camera, changes FOV,
//! moves native first-person arms, or alters a skeleton's scale or rotation.

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct BodyAlignmentFrame {
    pub camera: [f32; 3],
    pub head: [f32; 3],
    /// Horizontal body-forward XY. Normalized here; no camera pitch is used.
    pub heading: [f32; 2],
    /// Unsuppressed cumulative skeleton world scale, not a local bone scale.
    pub scale: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct BodyAlignmentOptions {
    pub alignment_enabled: u32,
    /// Positive moves the body behind the eye, in skeleton units at scale one.
    pub body_backset: f32,
    /// Positive moves the body to its right, in skeleton units at scale one.
    pub body_side: f32,
}
impl Default for BodyAlignmentOptions {
    fn default() -> Self {
        // Dormant until the separately gated first-person experiment is enabled.
        Self {
            alignment_enabled: 1,
            body_backset: 12.0,
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
    /// Camera Z minus unsuppressed head Z, for diagnostics only. Never applied.
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
            .all(|v| v.is_finite() && v.abs() <= 1e8)
        || !frame.heading.iter().all(|v| v.is_finite())
        || !frame.scale.is_finite()
        || !(0.1..=10.0).contains(&frame.scale)
    {
        return invalid;
    }
    let heading_length = frame.heading[0].hypot(frame.heading[1]);
    if !heading_length.is_finite() || heading_length < 0.0001 {
        return invalid;
    }
    let forward = frame.heading.map(|v| v / heading_length);
    let right = [forward[1], -forward[0]];
    let separation = [
        frame.camera[0] - frame.head[0],
        frame.camera[1] - frame.head[1],
    ];
    // An implausible native gap usually indicates stale/mismatched nodes. Do not
    // disguise it by saturating a correction or drag the actor's body across a cell.
    if separation[0].hypot(separation[1]) > 80.0 * frame.scale {
        return invalid;
    }
    let mut result = BodyAlignmentResult {
        vertical_error: frame.camera[2] - frame.head[2],
        valid: 1,
        ..invalid
    };
    if options.alignment_enabled == 0 {
        return result;
    }
    for i in 0..2 {
        result.translation[i] = separation[i] - forward[i] * options.body_backset * frame.scale
            + right[i] * options.body_side * frame.scale;
    }
    // The relative bound follows from the maximum accepted separation and
    // options; 128 world units is an additional conservative displacement cap.
    // A giant/custom rig that exceeds it keeps native placement. The small margin
    // over 80+hypot(40,20) avoids rejecting a valid endpoint through float rounding.
    let limit = (124.722 * frame.scale).min(128.0);
    if !result.translation.iter().all(|v| v.is_finite())
        || result.translation[0].hypot(result.translation[1]) > limit
    {
        return invalid;
    }
    // Z is deliberately zero: following an animated head vertically would float
    // the feet above the ground. Horizontal correction still needs visual testing.
    result
}

#[no_mangle]
pub extern "C" fn cc_align_body(
    frame: BodyAlignmentFrame,
    options: BodyAlignmentOptions,
) -> BodyAlignmentResult {
    align_body(frame, options)
}
