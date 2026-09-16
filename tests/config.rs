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
    assert_eq!(c.profiles[2].offset, [0.0; 3]);
    assert_eq!(c.profiles[2].half_life, 0.0);
    assert_eq!(c.enabled, 0);
}
