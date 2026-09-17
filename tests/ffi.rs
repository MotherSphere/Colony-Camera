use colony_camera::*;
use std::mem::{align_of, size_of};
#[test]
fn abi_layout_is_fixed_width_and_padding_free() {
    assert_eq!((size_of::<State>(), align_of::<State>()), (64, 4));
    assert_eq!((size_of::<Frame>(), align_of::<Frame>()), (40, 4));
    assert_eq!((size_of::<Profile>(), align_of::<Profile>()), (40, 4));
    assert_eq!((size_of::<Config>(), align_of::<Config>()), (308, 4));
    assert_eq!(size_of::<Coordinator>(), 8);
    assert_eq!(size_of::<Context>(), 24);
    assert_eq!(size_of::<Decision>(), 24);
}
#[test]
fn ffi_roundtrip_and_capacity_query_leave_partial_output_unchanged() {
    let config = Config::default();
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
