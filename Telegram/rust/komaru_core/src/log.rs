//! Messages for the client log, passed through a callback from C++.

use std::sync::atomic::{AtomicUsize, Ordering};

pub type Logger = unsafe extern "C" fn(text: *const u8, size: usize);

static LOGGER: AtomicUsize = AtomicUsize::new(0);

pub fn set(logger: Option<Logger>) {
    LOGGER.store(logger.map_or(0, |f| f as usize), Ordering::Release);
}

pub fn write(text: &str) {
    let raw = LOGGER.load(Ordering::Acquire);
    if raw == 0 {
        return;
    }
    // Only ever stored from a valid `Logger` in `set`.
    let logger: Logger = unsafe { std::mem::transmute(raw) };
    unsafe { logger(text.as_ptr(), text.len()) };
}

macro_rules! log {
    ($($arg:tt)*) => {
        $crate::log::write(&format!($($arg)*))
    };
}
pub(crate) use log;
