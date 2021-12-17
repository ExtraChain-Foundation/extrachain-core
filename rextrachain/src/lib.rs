extern "C" {
    fn extrachain_wipe();
}

#[no_mangle]
pub extern "C" fn number_42() -> i32 {
    unsafe { extrachain_wipe() }
    42
}
