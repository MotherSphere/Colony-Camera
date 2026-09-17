//! One owner for native, third-person and (when available) first-person updates.
pub const NATIVE: u32 = 0;
pub const THIRD_PERSON: u32 = 1;
pub const FIRST_PERSON: u32 = 2;
pub const AVAILABLE: u32 = 1 << 0;
pub const CONTROLS: u32 = 1 << 1;
pub const MENU: u32 = 1 << 2;
pub const DEAD: u32 = 1 << 3;
pub const RAGDOLL: u32 = 1 << 4;
pub const KILLMOVE: u32 = 1 << 5;
pub const MOUNTED: u32 = 1 << 6;
pub const FURNITURE: u32 = 1 << 7;
pub const TRANSFORMED: u32 = 1 << 8;
pub const SCRIPTED: u32 = 1 << 9;
pub const AIMING: u32 = 1 << 10;
pub const WEAPON_DRAWN: u32 = 1 << 11;
pub const SPRINTING: u32 = 1 << 12;
pub const SNEAKING: u32 = 1 << 13;
pub const SWIMMING: u32 = 1 << 14;
pub const IN_AIR: u32 = 1 << 15;
pub const EXPLORATION: u32 = 0;
pub const COMBAT: u32 = 1;
pub const AIM: u32 = 2;
pub const SPRINT: u32 = 3;
pub const SNEAK: u32 = 4;
pub const SWIM: u32 = 5;
pub const AIRBORNE: u32 = 6;
pub const PROFILE_COUNT: usize = 7;
pub const LOCOMOTION_MASK: u32 = (1 << SPRINT) | (1 << SNEAK) | (1 << SWIM) | (1 << AIRBORNE);
pub const ACTIVE: u32 = 0;
pub const DISABLED: u32 = 1;
pub const UNAVAILABLE: u32 = 2;
pub const MENU_OPEN: u32 = 3;
pub const CONTROLS_UNAVAILABLE: u32 = 4;
pub const DEATH: u32 = 5;
pub const RAGDOLL_STATE: u32 = 6;
pub const KILLMOVE_STATE: u32 = 7;
pub const SPECIAL_STATE: u32 = 8;
pub const NATIVE_STATE: u32 = 9;
pub const FIRST_PERSON_DISABLED: u32 = 10;
pub const FIRST_PERSON_UNAVAILABLE: u32 = 11;
pub const INVALID_CONTEXT: u32 = 12;
pub const THIRD_PERSON_DISABLED: u32 = 13;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Coordinator {
    pub owner: u32,
    pub initialized: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Context {
    pub camera_mode: u32,
    pub flags: u32,
    pub enabled: u32,
    pub first_person_enabled: u32,
    pub first_person_ready: u32,
    pub locomotion_profiles: u32,
    pub third_person_enabled: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Decision {
    pub next: Coordinator,
    pub owner: u32,
    pub profile: u32,
    pub reset: u32,
    pub reason: u32,
}
pub fn coordinate(previous: Coordinator, context: Context) -> Decision {
    let f = context.flags;
    // Availability and explicit native fallbacks always beat action profiles.
    let reason = if context.camera_mode > FIRST_PERSON
        || f & !0xffff != 0
        || [
            context.enabled,
            context.first_person_enabled,
            context.first_person_ready,
            context.third_person_enabled,
        ]
        .iter()
        .any(|v| *v > 1)
        || context.locomotion_profiles & !LOCOMOTION_MASK != 0
    {
        INVALID_CONTEXT
    } else if context.enabled == 0 {
        DISABLED
    } else if f & AVAILABLE == 0 {
        UNAVAILABLE
    } else if f & MENU != 0 {
        MENU_OPEN
    } else if f & CONTROLS == 0 {
        CONTROLS_UNAVAILABLE
    } else if f & DEAD != 0 {
        DEATH
    } else if f & RAGDOLL != 0 {
        RAGDOLL_STATE
    } else if f & KILLMOVE != 0 {
        KILLMOVE_STATE
    } else if f & (MOUNTED | FURNITURE | TRANSFORMED | SCRIPTED) != 0 {
        SPECIAL_STATE
    } else if context.camera_mode == NATIVE {
        NATIVE_STATE
    } else if context.camera_mode == THIRD_PERSON && context.third_person_enabled == 0 {
        THIRD_PERSON_DISABLED
    } else if context.camera_mode == FIRST_PERSON && context.first_person_enabled == 0 {
        FIRST_PERSON_DISABLED
    } else if context.camera_mode == FIRST_PERSON && context.first_person_ready == 0 {
        FIRST_PERSON_UNAVAILABLE
    } else {
        ACTIVE
    };
    let owner = if reason == ACTIVE {
        context.camera_mode
    } else {
        NATIVE
    };
    // Aim has absolute precedence. Locomotion profiles are explicit opt-ins;
    // absent old INI sections retain the original combat/exploration behavior.
    let locomotion = [
        (SWIMMING, SWIM),
        (SNEAKING, SNEAK),
        (SPRINTING, SPRINT),
        (IN_AIR, AIRBORNE),
    ]
    .into_iter()
    .find(|(flag, profile)| f & flag != 0 && context.locomotion_profiles & (1 << profile) != 0)
    .map(|(_, profile)| profile);
    let profile = if owner != THIRD_PERSON {
        // First-person rendering never consumes third-person motion profiles.
        EXPLORATION
    } else if f & AIMING != 0 {
        AIM
    } else if let Some(profile) = locomotion {
        profile
    } else if f & WEAPON_DRAWN != 0 {
        COMBAT
    } else {
        EXPLORATION
    };
    Decision {
        next: Coordinator {
            owner,
            initialized: 1,
        },
        owner,
        profile,
        reset: u32::from(previous.initialized != 1 || previous.owner != owner),
        reason,
    }
}
#[no_mangle]
pub extern "C" fn cc_coordinate(previous: Coordinator, context: Context) -> Decision {
    coordinate(previous, context)
}
