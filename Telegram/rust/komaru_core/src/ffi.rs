//! The C API declared in `include/komaru_core.h`.
//!
//! Rows are handed to C++ through callbacks. The bytes they point to stay
//! valid only during the call, so C++ copies what it keeps.

use std::ffi::c_void;
use std::panic::{catch_unwind, AssertUnwindSafe};

use crate::db::{self, Bytes, Exclusion, Filter, FilterQuery, Message, MessageKind};
use crate::log::{self, log, Logger};
use crate::filter::Pattern;

type MessageSink = unsafe extern "C" fn(context: *mut c_void, row: *const Message);
type FilterSink = unsafe extern "C" fn(context: *mut c_void, row: *const Filter);
type ExclusionSink = unsafe extern "C" fn(context: *mut c_void, row: *const Exclusion);

/// Never lets a panic cross into C++.
fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|_| {
        log!("komaru_core: panic caught at the C boundary");
        fallback
    })
}

unsafe fn text<'a>(data: *const u8, size: usize) -> &'a [u8] {
    Bytes { data, size }.get()
}

#[no_mangle]
pub extern "C" fn komaru_set_logger(logger: Option<Logger>) {
    log::set(logger);
}

/// # Safety
/// `path` must point to `size` bytes of UTF-8.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_initialize(path: *const u8, size: usize) {
    let path = String::from_utf8_lossy(text(path, size)).into_owned();
    guard((), || db::initialize(&path))
}

/// # Safety
/// `message` must be a valid row whose bytes are readable.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_add_message(edited: u8, message: *const Message) {
    let Some(message) = message.as_ref() else {
        return;
    };
    let kind = if edited != 0 { MessageKind::Edited } else { MessageKind::Deleted };
    guard((), || db::add_message(kind, message))
}

/// # Safety
/// `sink` is called with `context` for every row.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_edited_messages(
    user_id: i64,
    dialog_id: i64,
    message_id: i64,
    min_id: i64,
    max_id: i64,
    limit: i64,
    sink: MessageSink,
    context: *mut c_void,
) {
    guard((), || {
        db::edited_messages(user_id, dialog_id, message_id, min_id, max_id, limit, &mut |row| {
            sink(context, row)
        })
    })
}

#[no_mangle]
pub extern "C" fn komaru_db_has_revisions(user_id: i64, dialog_id: i64, message_id: i64) -> u8 {
    guard(0, || u8::from(db::has_revisions(user_id, dialog_id, message_id)))
}

/// # Safety
/// `query` must point to `query_size` bytes, `sink` is called with
/// `context` for every row.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_deleted_messages(
    user_id: i64,
    dialog_id: i64,
    topic_id: i64,
    min_id: i64,
    max_id: i64,
    limit: i64,
    query: *const u8,
    query_size: usize,
    sink: MessageSink,
    context: *mut c_void,
) {
    let query = text(query, query_size);
    guard((), || {
        db::deleted_messages(
            user_id,
            dialog_id,
            topic_id,
            min_id,
            max_id,
            limit,
            query,
            &mut |row| sink(context, row),
        )
    })
}

#[no_mangle]
pub extern "C" fn komaru_db_has_deleted_messages(user_id: i64, dialog_id: i64, topic_id: i64) -> u8 {
    guard(0, || u8::from(db::has_deleted_messages(user_id, dialog_id, topic_id)))
}

#[no_mangle]
pub extern "C" fn komaru_db_remove_deleted_message(user_id: i64, dialog_id: i64, message_id: i64) {
    guard((), || db::remove_deleted_message(user_id, dialog_id, message_id))
}

#[no_mangle]
pub extern "C" fn komaru_db_clear_deleted_messages(user_id: i64, dialog_id: i64, topic_id: i64) {
    guard((), || db::clear_deleted_messages(user_id, dialog_id, topic_id))
}

pub const FILTERS_ALL: i32 = 0;
pub const FILTERS_SHARED: i32 = 1;
pub const FILTERS_FOR_DIALOG: i32 = 2;
pub const FILTERS_EXCLUDED_FOR_DIALOG: i32 = 3;

/// # Safety
/// `sink` is called with `context` for every row.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_filters(
    which: i32,
    dialog_id: i64,
    sink: FilterSink,
    context: *mut c_void,
) {
    let query = match which {
        FILTERS_ALL => FilterQuery::All,
        FILTERS_SHARED => FilterQuery::Shared,
        FILTERS_FOR_DIALOG => FilterQuery::ForDialog(dialog_id),
        FILTERS_EXCLUDED_FOR_DIALOG => FilterQuery::ExcludedForDialog(dialog_id),
        _ => return,
    };
    guard((), || db::filters(query, &mut |row| sink(context, row)))
}

/// # Safety
/// `sink` is called with `context` for every row.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_exclusions(sink: ExclusionSink, context: *mut c_void) {
    guard((), || db::exclusions(&mut |row| sink(context, row)))
}

/// # Safety
/// `filter` must be a valid row whose bytes are readable.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_put_filter(filter: *const Filter) {
    if let Some(filter) = filter.as_ref() {
        guard((), || db::put_filter(filter))
    }
}

/// # Safety
/// `filter` must be a valid row whose bytes are readable.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_update_filter(filter: *const Filter) {
    if let Some(filter) = filter.as_ref() {
        guard((), || db::update_filter(filter))
    }
}

/// # Safety
/// `id` must point to `size` bytes.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_delete_filter(id: *const u8, size: usize) {
    let id = text(id, size);
    guard((), || db::delete_filter(id))
}

/// # Safety
/// `filter_id` must point to `size` bytes.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_add_exclusion(dialog_id: i64, filter_id: *const u8, size: usize) {
    let filter_id = text(filter_id, size);
    guard((), || db::add_exclusion(dialog_id, filter_id))
}

/// # Safety
/// `filter_id` must point to `size` bytes.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_delete_exclusions_of_filter(filter_id: *const u8, size: usize) {
    let filter_id = text(filter_id, size);
    guard((), || db::delete_exclusions_of_filter(filter_id))
}

/// # Safety
/// `filter_id` must point to `size` bytes.
#[no_mangle]
pub unsafe extern "C" fn komaru_db_delete_exclusion(dialog_id: i64, filter_id: *const u8, size: usize) {
    let filter_id = text(filter_id, size);
    guard((), || db::delete_exclusion(dialog_id, filter_id))
}

#[no_mangle]
pub extern "C" fn komaru_db_delete_all_filters() {
    guard((), db::delete_all_filters)
}

#[no_mangle]
pub extern "C" fn komaru_db_delete_all_exclusions() {
    guard((), db::delete_all_exclusions)
}

#[no_mangle]
pub extern "C" fn komaru_db_has_filters() -> u8 {
    guard(0, || u8::from(db::has_filters()))
}

#[no_mangle]
pub extern "C" fn komaru_db_has_per_dialog_filters() -> u8 {
    guard(0, || u8::from(db::has_per_dialog_filters()))
}

/// # Safety
/// `pattern` must point to `size` bytes.
#[no_mangle]
pub unsafe extern "C" fn komaru_filter_compile(
    pattern: *const u8,
    size: usize,
    case_insensitive: u8,
) -> *mut Pattern {
    let pattern = String::from_utf8_lossy(text(pattern, size)).into_owned();
    guard(std::ptr::null_mut(), || {
        Pattern::compile(&pattern, case_insensitive != 0)
            .map_or(std::ptr::null_mut(), |p| Box::into_raw(Box::new(p)))
    })
}

/// # Safety
/// `pattern` comes from komaru_filter_compile, `text` points to `size`
/// bytes of UTF-8.
#[no_mangle]
pub unsafe extern "C" fn komaru_filter_find(
    pattern: *const Pattern,
    text_data: *const u8,
    size: usize,
) -> u8 {
    let Some(pattern) = pattern.as_ref() else {
        return 0;
    };
    let text = text(text_data, size);
    guard(0, || u8::from(pattern.find(text)))
}

/// # Safety
/// `pattern` comes from komaru_filter_compile and is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn komaru_filter_free(pattern: *mut Pattern) {
    if !pattern.is_null() {
        drop(Box::from_raw(pattern));
    }
}

type RangeSink = unsafe extern "C" fn(context: *mut c_void, offset: i64, size: i64);
type ColorSink = unsafe extern "C" fn(context: *mut c_void, end: usize, digits: usize);
type RateLookup = unsafe extern "C" fn(
    context: *mut c_void,
    coin: *const u8,
    coin_size: usize,
    fiat: *const u8,
    fiat_size: usize,
) -> f64;

/// # Safety
/// `path` must point to `size` bytes of UTF-8, `sink` is called with
/// `context` for every range.
#[no_mangle]
pub unsafe extern "C" fn komaru_metadata_find(
    path: *const u8,
    size: usize,
    sink: RangeSink,
    context: *mut c_void,
) {
    let path = String::from_utf8_lossy(text(path, size)).into_owned();
    for (offset, size) in guard(Vec::new(), || crate::metadata::find(&path)) {
        sink(context, offset, size);
    }
}

/// # Safety
/// `data` must point to `size` UTF-16 units, `sink` is called with
/// `context` for every color.
#[no_mangle]
pub unsafe extern "C" fn komaru_color_find(
    data: *const u16,
    size: usize,
    sink: ColorSink,
    context: *mut c_void,
) {
    if data.is_null() || size == 0 {
        return;
    }
    let units = std::slice::from_raw_parts(data, size);
    for (end, digits) in guard(Vec::new(), || crate::color::find(units)) {
        sink(context, end, digits);
    }
}

/// # Safety
/// `query` must point to `size` bytes of UTF-8, `out` to `capacity`
/// writable bytes, `lookup` is called with `context` at most once.
#[no_mangle]
pub unsafe extern "C" fn komaru_rate_completion(
    query: *const u8,
    size: usize,
    lookup: RateLookup,
    context: *mut c_void,
    out: *mut u8,
    capacity: usize,
) -> usize {
    let query = String::from_utf8_lossy(text(query, size)).into_owned();
    let result = guard(None, || {
        crate::rates::completion(&query, |coin, fiat| {
            lookup(context, coin.as_ptr(), coin.len(), fiat.as_ptr(), fiat.len())
        })
    });
    match result {
        Some(result) if !out.is_null() && result.len() <= capacity => {
            std::ptr::copy_nonoverlapping(result.as_ptr(), out, result.len());
            result.len()
        }
        _ => 0,
    }
}

#[no_mangle]
pub extern "C" fn komaru_pack_owner_id(id: u64) -> u64 {
    crate::packs::owner_id(id)
}
