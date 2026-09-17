use colony_camera::*;
fn context(flags: u32) -> Context {
    Context {
        camera_mode: THIRD_PERSON,
        enabled: 1,
        flags: AVAILABLE | CONTROLS | flags,
        ..Context::default()
    }
}
#[test]
fn native_fallbacks_have_deterministic_precedence() {
    let mut c = context(DEAD | RAGDOLL | KILLMOVE | MOUNTED | MENU | AIMING);
    c.enabled = 0;
    assert_eq!(coordinate(Coordinator::default(), c).reason, DISABLED);
    c.enabled = 1;
    c.flags &= !AVAILABLE;
    assert_eq!(coordinate(Coordinator::default(), c).reason, UNAVAILABLE);
    c.flags |= AVAILABLE;
    assert_eq!(coordinate(Coordinator::default(), c).reason, MENU_OPEN);
    c.flags &= !MENU;
    c.flags &= !CONTROLS;
    assert_eq!(
        coordinate(Coordinator::default(), c).reason,
        CONTROLS_UNAVAILABLE
    );
    c.flags |= CONTROLS;
    assert_eq!(coordinate(Coordinator::default(), c).reason, DEATH);
    c.flags &= !DEAD;
    assert_eq!(coordinate(Coordinator::default(), c).reason, RAGDOLL_STATE);
    c.flags &= !RAGDOLL;
    assert_eq!(coordinate(Coordinator::default(), c).reason, KILLMOVE_STATE);
    c.flags &= !KILLMOVE;
    assert_eq!(coordinate(Coordinator::default(), c).reason, SPECIAL_STATE);
}
#[test]
fn every_special_state_remains_native_even_with_aim_or_renderer_ready() {
    for flag in [
        DEAD,
        RAGDOLL,
        KILLMOVE,
        MOUNTED,
        FURNITURE,
        TRANSFORMED,
        SCRIPTED,
    ] {
        for mode in [THIRD_PERSON, FIRST_PERSON] {
            let c = Context {
                camera_mode: mode,
                first_person_enabled: 1,
                first_person_ready: 1,
                ..context(flag | AIMING)
            };
            let decision = coordinate(
                Coordinator {
                    owner: mode,
                    initialized: 1,
                },
                c,
            );
            assert_eq!(decision.owner, NATIVE);
            assert_eq!(decision.reset, 1);
        }
    }
}
#[test]
fn perspective_changes_reset_once_and_profile_changes_blend() {
    let mut c = context(0);
    let first = coordinate(Coordinator::default(), c);
    assert_eq!(first.owner, THIRD_PERSON);
    assert_eq!(first.reset, 1);
    c.flags |= WEAPON_DRAWN;
    let combat = coordinate(first.next, c);
    assert_eq!(combat.profile, COMBAT);
    assert_eq!(combat.reset, 0);
    c.camera_mode = FIRST_PERSON;
    let native = coordinate(combat.next, c);
    assert_eq!(native.reason, FIRST_PERSON_DISABLED);
    assert_eq!(native.owner, NATIVE);
    assert_eq!(native.reset, 1);
    c.first_person_enabled = 1;
    let unavailable = coordinate(native.next, c);
    assert_eq!(unavailable.reason, FIRST_PERSON_UNAVAILABLE);
    assert_eq!(unavailable.reset, 0);
    c.first_person_ready = 1;
    let first_person = coordinate(unavailable.next, c);
    assert_eq!(first_person.owner, FIRST_PERSON);
    assert_eq!(first_person.reset, 1);
    assert_eq!(coordinate(first_person.next, c).reset, 0);
}
#[test]
fn action_priority_is_aim_swim_sneak_sprint_airborne_combat_exploration() {
    let mut c = context(AIMING | SWIMMING | SNEAKING | SPRINTING | IN_AIR | WEAPON_DRAWN);
    c.locomotion_profiles = LOCOMOTION_MASK;
    for (profile, remove) in [
        (AIM, AIMING),
        (SWIM, SWIMMING),
        (SNEAK, SNEAKING),
        (SPRINT, SPRINTING),
        (AIRBORNE, IN_AIR),
        (COMBAT, WEAPON_DRAWN),
        (EXPLORATION, 0),
    ] {
        assert_eq!(coordinate(Coordinator::default(), c).profile, profile);
        c.flags &= !remove;
    }
    let c = context(SNEAKING | SWIMMING | WEAPON_DRAWN);
    assert_eq!(coordinate(Coordinator::default(), c).profile, COMBAT);
}
#[test]
fn unknown_enum_values_and_flag_bits_fail_closed() {
    for c in [
        Context {
            camera_mode: 33,
            ..context(0)
        },
        Context {
            enabled: 2,
            ..context(0)
        },
        Context {
            first_person_ready: 2,
            ..context(0)
        },
        Context {
            locomotion_profiles: 1,
            ..context(0)
        },
        context(1 << 31),
    ] {
        let d = coordinate(Coordinator::default(), c);
        assert_eq!(d.owner, NATIVE);
        assert_eq!(d.reason, INVALID_CONTEXT);
    }
}
