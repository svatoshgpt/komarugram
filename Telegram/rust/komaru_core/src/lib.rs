//! Native parts of KomaruGram, linked into the client as a static library.
//!
//! The C API lives in `ffi.rs` and is declared for C++ in
//! `include/komaru_core.h`.

mod db;
mod filter;
mod ffi;
mod log;
mod sqlite;
