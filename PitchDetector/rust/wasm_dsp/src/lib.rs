#![allow(non_snake_case)]

// Minimal DSP exports without wasm-bindgen. Build with:
//   rustup target add wasm32-unknown-unknown
//   cargo build -p wasm_dsp --target wasm32-unknown-unknown --release

#[no_mangle]
pub extern "C" fn process_sample(x: f32) -> f32 {
    // Simple soft clip as placeholder DSP: y = tanh(gain*x)
    let gain = 1.5_f32;
    softsign(gain * x)
}

#[inline]
fn softsign(x: f32) -> f32 {
    x / (1.0 + x.abs())
}

// Process an in-place buffer: ptr points to f32 array of length len; applies gain
#[no_mangle]
pub extern "C" fn process_buffer(ptr: *mut f32, len: usize, gain: f32) {
    if ptr.is_null() || len == 0 { return; }
    let buf = unsafe { core::slice::from_raw_parts_mut(ptr, len) };
    for v in buf.iter_mut() {
        *v = softsign(gain * *v);
    }
}

// Provide a reusable internal work buffer to avoid JS-side allocations.
// Size supports up to 16384 frames (float32).
static mut WORK_BUFFER: [f32; 16384] = [0.0; 16384];

#[no_mangle]
pub extern "C" fn get_work_buffer_ptr() -> *mut f32 {
    unsafe { WORK_BUFFER.as_mut_ptr() }
}

#[no_mangle]
pub extern "C" fn process_work_buffer(len: usize, gain: f32) {
    let max_len = unsafe { WORK_BUFFER.len() };
    let n = core::cmp::min(len, max_len);
    let ptr = unsafe { WORK_BUFFER.as_mut_ptr() };
    unsafe {
        let buf = core::slice::from_raw_parts_mut(ptr, n);
        for v in buf.iter_mut() {
            *v = softsign(gain * *v);
        }
    }
}
