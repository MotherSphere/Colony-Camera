use colony_camera::*;
use std::mem::{align_of, offset_of, size_of};
#[test]
fn abi_layout_is_fixed_width_and_padding_free() {
    assert_eq!((size_of::<State>(), align_of::<State>()), (64, 4));
    assert_eq!((size_of::<Frame>(), align_of::<Frame>()), (40, 4));
    assert_eq!((size_of::<Profile>(), align_of::<Profile>()), (40, 4));
    assert_eq!((size_of::<Config>(), align_of::<Config>()), (4664, 4));
    assert_eq!(offset_of!(Config, third_person_enabled), 320);
    assert_eq!(
        (
            size_of::<BodyAlignmentFrame>(),
            align_of::<BodyAlignmentFrame>()
        ),
        (104, 4)
    );
    assert_eq!(offset_of!(BodyAlignmentFrame, body_root), 52);
    assert_eq!(offset_of!(BodyAlignmentFrame, body_rotation), 64);
    assert_eq!(offset_of!(BodyAlignmentFrame, movement), 100);
    assert_eq!(
        (
            size_of::<BodyAlignmentOptions>(),
            align_of::<BodyAlignmentOptions>()
        ),
        (12, 4)
    );
    assert_eq!(
        (
            size_of::<BodyAlignmentResult>(),
            align_of::<BodyAlignmentResult>()
        ),
        (20, 4)
    );
    assert_eq!(size_of::<Coordinator>(), 8);
    assert_eq!(size_of::<Context>(), 28);
    assert_eq!(offset_of!(Context, third_person_enabled), 24);
    assert_eq!(size_of::<Decision>(), 24);
}
#[test]
fn ffi_roundtrip_and_capacity_query_leave_partial_output_unchanged() {
    let config = Config {
        first_person_enabled: 1,
        third_person_enabled: 0,
        ..Config::default()
    };
    let mut written = 0;
    unsafe {
        assert_eq!(
            cc_serialize_config(&config, std::ptr::null_mut(), 0, &mut written),
            4
        );
        let required = written;
        let mut bytes = vec![0xa5; required];
        assert_eq!(
            cc_serialize_config(&config, bytes.as_mut_ptr(), required - 1, &mut written),
            4
        );
        assert_eq!(written, required);
        assert!(bytes.iter().all(|b| *b == 0xa5));
        assert_eq!(
            cc_serialize_config(&config, bytes.as_mut_ptr(), required, &mut written),
            0
        );
        let mut parsed = Config::default();
        assert_eq!(cc_parse_config(bytes.as_ptr(), written, &mut parsed), 0);
        assert_eq!(parsed, config);
    }
}
#[test]
fn ffi_rejects_null_unaligned_overlap_overflow_and_bad_config_atomically() {
    let mut config = Config::default();
    let text = b"[general]\nenabled=false";
    let mut written = 987;
    let mut storage = vec![0u32; 100];
    let misaligned = unsafe { storage.as_mut_ptr().cast::<u8>().add(1).cast::<Config>() };
    unsafe {
        assert_eq!(
            cc_parse_config(text.as_ptr(), text.len(), std::ptr::null_mut()),
            1
        );
        assert_eq!(cc_parse_config(std::ptr::null(), 0, &mut config), 1);
        assert_eq!(cc_parse_config(text.as_ptr(), text.len(), misaligned), 1);
        let config_ptr = &mut config as *mut Config;
        assert_eq!(cc_parse_config(config_ptr.cast(), 1, config_ptr), 1);
        assert_eq!(
            cc_parse_config(text.as_ptr(), MAX_CONFIG_BYTES + 1, config_ptr),
            1
        );
        assert_eq!(cc_parse_config(usize::MAX as *const u8, 2, config_ptr), 1);
        assert_eq!(cc_validate_config(std::ptr::null()), 1);
        assert_eq!(cc_validate_config(misaligned), 1);
        assert_eq!(
            cc_serialize_config(config_ptr, config_ptr.cast(), 1, &mut written),
            1
        );
        assert_eq!(
            cc_serialize_config(config_ptr, std::ptr::null_mut(), 4, &mut written),
            1
        );
        assert_eq!(
            cc_serialize_config(config_ptr, std::ptr::null_mut(), 0, config_ptr.cast()),
            1
        );
        assert_eq!(written, 987);
        assert_eq!(config, Config::default());
        assert_eq!(cc_parse_config([0xff].as_ptr(), 1, &mut config), 2);
        assert_eq!(config, Config::default());
        config.enabled = 2;
        assert_eq!(cc_validate_config(&config), 3);
        assert_eq!(
            cc_serialize_config(&config, std::ptr::null_mut(), 0, &mut written),
            3
        );
        assert_eq!(written, 987);
    }
}

#[test]
fn smoothcam_import_reports_capacity_and_rejection_without_partial_application() {
    let source = br#"{"name":"ABI test","config":{"standing":{"sideOffset":25}}}"#;
    let current = Config {
        first_person_enabled: 1,
        ..Config::default()
    };
    let mut output = current;
    let mut written = 0;
    unsafe {
        assert_eq!(
            cc_import_smoothcam(
                source.as_ptr(),
                source.len(),
                &current,
                &mut output,
                std::ptr::null_mut(),
                0,
                &mut written
            ),
            4
        );
        assert!(written > 0);
        assert_eq!(output, current);
        let mut report = vec![0xa5; written + 16];
        let required = written;
        assert_eq!(
            cc_import_smoothcam(
                source.as_ptr(),
                source.len(),
                &current,
                &mut output,
                report.as_mut_ptr(),
                required - 1,
                &mut written
            ),
            4
        );
        assert!(report.iter().all(|v| *v == 0xa5));
        assert_eq!(output, current);
        assert_eq!(
            cc_import_smoothcam(
                source.as_ptr(),
                source.len(),
                &current,
                &mut output,
                report.as_mut_ptr(),
                required,
                &mut written
            ),
            0
        );
        assert_eq!(output.first_person_enabled, 1);
        assert_eq!(output.keys, current.keys);
        assert_eq!(output.third.profiles[0].offset[0], 25.0);
        assert_eq!(output.third.enabled, 1);
        assert!(report[required..].iter().all(|v| *v == 0xa5));
        assert!(std::str::from_utf8(&report[..required]).is_ok());
        let saved = output;
        let bad = br#"{"config":{"standing":{"sideOffset":"twenty"}}}"#;
        report.resize(16384, 0);
        assert_eq!(
            cc_import_smoothcam(
                bad.as_ptr(),
                bad.len(),
                &current,
                &mut output,
                report.as_mut_ptr(),
                report.len(),
                &mut written
            ),
            3
        );
        assert_eq!(output, saved);
        assert!(std::str::from_utf8(&report[..written])
            .unwrap()
            .contains("rejected"));
        let ptr = &mut output as *mut Config;
        assert_eq!(
            cc_import_smoothcam(
                source.as_ptr(),
                source.len(),
                ptr,
                ptr,
                report.as_mut_ptr(),
                report.len(),
                &mut written
            ),
            1
        );
        assert_eq!(
            cc_import_smoothcam(
                source.as_ptr(),
                source.len(),
                &current,
                &mut output,
                usize::MAX as *mut u8,
                2,
                &mut written
            ),
            1
        );
        assert_eq!(
            cc_import_smoothcam(
                source.as_ptr(),
                MAX_CONFIG_BYTES + 1,
                &current,
                &mut output,
                report.as_mut_ptr(),
                report.len(),
                &mut written
            ),
            1
        );
        assert_eq!(output, saved);
    }
}
