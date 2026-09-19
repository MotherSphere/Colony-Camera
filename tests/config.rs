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
    assert!(text.contains("format_version=5"));
    assert_eq!(parse_config(&text).unwrap(), c);
}

#[test]
fn rejects_new_invalid_values_without_silently_accepting_typos() {
    for text in [
        "[general]\nformat_version=6",
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

#[test]
fn legacy_body_experiment_gets_alignment_without_changing_enablement_or_bindings() {
    for version in [1, 2, 3] {
        let text = format!("[general]\nformat_version={version}\ntoggle_key=80\nshoulder_key=81\nreload_key=82\nmenu_key=83\n[first_person]\nenabled=true");
        let config = parse_config(&text).unwrap();
        assert_eq!(config.first_person_enabled, 1);
        assert_eq!(config.third_person_enabled, 1);
        assert_eq!(config.keys, [80, 81, 82]);
        assert_eq!(config.menu_key, 83);
        assert_eq!(
            config.body_alignment,
            colony_camera::BodyAlignmentOptions::default()
        );
        assert_eq!(config.body_alignment.alignment_enabled, 1);
    }
    let old_off = parse_config("[first_person]\nenabled=false").unwrap();
    assert_eq!(old_off.first_person_enabled, 0);
    assert_eq!(colony_camera::Config::default().first_person_enabled, 0);
}

#[test]
fn body_alignment_options_roundtrip_and_reject_invalid_values() {
    let config = parse_config("[first_person]\nenabled=true\nalignment_enabled=false\nbody_backset=27.125\nbody_side=-9.5").unwrap();
    assert_eq!(config.body_alignment.alignment_enabled, 0);
    assert_eq!(config.body_alignment.body_backset, 27.125);
    assert_eq!(config.body_alignment.body_side, -9.5);
    let text = colony_camera::serialize_config(&config).unwrap();
    assert!(text.contains("format_version=5"));
    assert_eq!(parse_config(&text).unwrap(), config);
    for value in [
        "alignment_enabled=1",
        "body_backset=-1",
        "body_backset=41",
        "body_backset=NaN",
        "body_side=-21",
        "body_side=21",
        "body_side=inf",
        "body_forward=12",
    ] {
        assert!(
            parse_config(&format!("[first_person]\n{value}")).is_err(),
            "{value}"
        );
    }
    let mut invalid = config;
    invalid.body_alignment.body_backset = f32::NAN;
    assert!(!invalid.valid());
    assert!(colony_camera::serialize_config(&invalid).is_err());
}

#[test]
fn third_person_switch_roundtrips_independently_of_first_person_and_master() {
    use colony_camera::{serialize_config, Config};
    assert_eq!(Config::default().third_person_enabled, 1);
    assert_eq!(parse_config("").unwrap().third_person_enabled, 1);
    for master in [0, 1] {
        for first_person in [0, 1] {
            for third_person in [0, 1] {
                let config = Config {
                    enabled: master,
                    first_person_enabled: first_person,
                    third_person_enabled: third_person,
                    ..Config::default()
                };
                let text = serialize_config(&config).unwrap();
                assert!(text.contains("format_version=5"));
                assert!(text.contains(&format!("[third_person]\nenabled={}", third_person != 0)));
                assert_eq!(parse_config(&text).unwrap(), config);
            }
        }
    }
    for text in [
        "[third_person]\nenabled=1",
        "[third_person]\nsmoothing_enabled=false",
        "[third_person]\nenabled=true\nenabled=false",
    ] {
        assert!(parse_config(text).is_err(), "{text}");
    }
    let invalid = Config {
        third_person_enabled: 2,
        ..Config::default()
    };
    assert!(!invalid.valid());
    assert!(serialize_config(&invalid).is_err());
}

#[test]
fn advanced_settings_roundtrip_and_legacy_stays_legacy() {
    use colony_camera::{serialize_config, Config};
    let old = parse_config("[general]\nformat_version=4\n[first_person]\nenabled=true").unwrap();
    assert_eq!(old.third.enabled, 0);
    let mut c = Config::default();
    c.third.enabled = 1;
    c.third.profiles[9].offset = [31.125, -24.5, 7.25];
    c.third.pitch_zoom = 32.0;
    let encoded = serialize_config(&c).unwrap();
    assert_eq!(parse_config(&encoded).unwrap(), c);
    assert_eq!(parse_config(&serialize_config(&old).unwrap()).unwrap(), old);
    let invalid = encoded.replace(
        &format!("\"pitch_curve\":{}", c.third.pitch_curve),
        "\"pitch_curve\":999",
    );
    assert_ne!(invalid, encoded);
    assert!(parse_config(&invalid).is_err());
}
