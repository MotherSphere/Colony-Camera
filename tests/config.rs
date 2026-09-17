use colony_camera::parse_config;
#[test]
fn rejects_unknown_duplicate_nonfinite_and_out_of_range_settings() {
    for text in [
        "[combat]\nhalf_life=NaN",
        "[combat]\nmax_lag=-1",
        "[combat]\nx=900",
        "[wrong]",
        "[combat]\nx=1\nx=2",
        "[general]\ntoggle_key=999",
        "[general]\ntoggle_key=29",
        "[general]\nshoulder_key=157",
        "[general]\nenabled=maybe",
        "[general]\ntoggle_key=120\nshoulder_key=120",
    ] {
        assert!(parse_config(text).is_err(), "{text}");
    }
}
#[test]
fn loads_partial_profiles_and_preserves_native_aim_by_default() {
    let c =
        parse_config("# comment\n[exploration]\nx=25\nhalf_life=0.12\n[general]\nenabled=false\n")
            .unwrap();
    assert_eq!(c.profiles[0].offset[0], 25.0);
    assert_eq!(c.profiles[0].half_life, 0.12);
    assert_eq!(c.profiles[0].offset_half_life, 0.12);
    assert_eq!(c.profiles[2].offset, [0.0; 3]);
    assert_eq!(c.profiles[2].half_life, 0.0);
    assert_eq!(c.enabled, 0);
}

#[test]
fn legacy_settings_preserve_bindings_and_do_not_activate_new_policies() {
    let c = parse_config("[general]\ntoggle_key=65\n[combat]\nhalf_life=0.2").unwrap();
    assert_eq!(c.keys[0], 65);
    assert_ne!(c.menu_key, 65);
    assert_eq!(c.locomotion_profiles, 0);
    assert_eq!(c.first_person_enabled, 0);
    assert_eq!(c.profiles[1].offset_half_life, 0.2);
}

#[test]
fn canonical_settings_roundtrip_every_profile_and_opt_in_field() {
    use colony_camera::{serialize_config, SNEAK, SPRINT};
    let input = "[general]\nformat_version=2\nmenu_key=50\n[first_person]\nenabled=true\n[sprint]\nzoom=32.125\noffset_half_life=0.1\nhalf_life=0.17\nfov_offset=-12\n[sneak]\nactive=false\nx=17";
    let c = parse_config(input).unwrap();
    assert_eq!(c.locomotion_profiles & (1 << SPRINT), 1 << SPRINT);
    assert_eq!(c.locomotion_profiles & (1 << SNEAK), 0);
    assert_eq!(c.profiles[SPRINT as usize].offset_half_life, 0.1);
    assert_eq!(c.first_person_enabled, 1);
    let text = serialize_config(&c).unwrap();
    assert!(text.contains("format_version=2"));
    assert_eq!(parse_config(&text).unwrap(), c);
}

#[test]
fn rejects_new_invalid_values_without_silently_accepting_typos() {
    for text in [
        "[general]\nformat_version=3",
        "[general]\nmenu_key=66",
        "[sprint]\nactive=1",
        "[first_person]\nenabled=1",
        "[first_person]\nrender_body=true",
        "[swim]\nfov_half_life=NaN",
        "[airborne]\nzoom=301",
        "[exploration]\nfov_offset=-61",
    ] {
        assert!(parse_config(text).is_err(), "{text}");
    }
}
