//! A small safe wrapper over the SQLite C API.
//!
//! The functions come from `ayu/libs/sqlite/sqlite3.c`, which the client
//! compiles anyway, handed over as a table by `komaru_set_sqlite`. The crate
//! is linked into a library that is also built as a DLL, so it must not
//! leave SQLite symbols for the final link to resolve. Tests take the
//! system library instead.

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::fmt;
use std::ptr;
use std::sync::OnceLock;

#[repr(C)]
struct RawDb {
    _private: [u8; 0],
}

#[repr(C)]
struct RawStmt {
    _private: [u8; 0],
}

const SQLITE_OK: c_int = 0;
const SQLITE_ROW: c_int = 100;
const SQLITE_DONE: c_int = 101;
const SQLITE_NULL: c_int = 5;
const SQLITE_OPEN_READWRITE: c_int = 0x0000_0002;
const SQLITE_OPEN_CREATE: c_int = 0x0000_0004;

// Makes SQLite copy the bound data before the call returns.
const SQLITE_TRANSIENT: isize = -1;

/// Declares the table `Api`, laid out as `KomaruSqlite` in the header, and
/// a wrapper of each function under its SQLite name.
macro_rules! sqlite_api {
    ($($name:ident($($arg:ident: $type:ty),*) $(-> $result:ty)?;)*) => {
        #[repr(C)]
        #[derive(Clone, Copy)]
        pub struct Api {
            $($name: unsafe extern "C" fn($($type),*) $(-> $result)?,)*
        }

        $(unsafe fn $name($($arg: $type),*) $(-> $result)? {
            (api().$name)($($arg),*)
        })*

        #[cfg(test)]
        mod system {
            use super::*;

            #[link(name = "sqlite3")]
            extern "C" {
                $(pub fn $name($($arg: $type),*) $(-> $result)?;)*
            }

            pub fn table() -> Api {
                Api { $($name,)* }
            }
        }
    };
}

sqlite_api! {
    sqlite3_initialize() -> c_int;
    sqlite3_open_v2(
        filename: *const c_char,
        db: *mut *mut RawDb,
        flags: c_int,
        vfs: *const c_char
    ) -> c_int;
    sqlite3_close_v2(db: *mut RawDb) -> c_int;
    sqlite3_errmsg(db: *mut RawDb) -> *const c_char;
    sqlite3_exec(
        db: *mut RawDb,
        sql: *const c_char,
        callback: *const c_void,
        argument: *mut c_void,
        error: *mut *mut c_char
    ) -> c_int;
    sqlite3_prepare_v2(
        db: *mut RawDb,
        sql: *const c_char,
        size: c_int,
        stmt: *mut *mut RawStmt,
        tail: *mut *const c_char
    ) -> c_int;
    sqlite3_finalize(stmt: *mut RawStmt) -> c_int;
    sqlite3_step(stmt: *mut RawStmt) -> c_int;
    sqlite3_bind_int64(stmt: *mut RawStmt, index: c_int, value: i64) -> c_int;
    sqlite3_bind_null(stmt: *mut RawStmt, index: c_int) -> c_int;
    sqlite3_bind_text(
        stmt: *mut RawStmt,
        index: c_int,
        data: *const c_char,
        size: c_int,
        destructor: isize
    ) -> c_int;
    sqlite3_bind_blob(
        stmt: *mut RawStmt,
        index: c_int,
        data: *const c_void,
        size: c_int,
        destructor: isize
    ) -> c_int;
    sqlite3_column_type(stmt: *mut RawStmt, column: c_int) -> c_int;
    sqlite3_column_int64(stmt: *mut RawStmt, column: c_int) -> i64;
    sqlite3_column_blob(stmt: *mut RawStmt, column: c_int) -> *const c_void;
    sqlite3_column_bytes(stmt: *mut RawStmt, column: c_int) -> c_int;
    sqlite3_free(pointer: *mut c_void);
}

static API: OnceLock<Api> = OnceLock::new();

/// Takes the functions to call; only the first table counts.
pub fn set_api(api: Api) {
    let _ = API.set(api);
}

fn api() -> &'static Api {
    #[cfg(test)]
    let _ = API.set(system::table());
    API.get().expect("komaru_set_sqlite was not called")
}

#[derive(Debug, Clone)]
pub struct Error(pub String);

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

pub type Result<T> = std::result::Result<T, Error>;

/// A value bound to a statement parameter.
#[derive(Clone, Copy)]
pub enum Value<'a> {
    Int(i64),
    Text(&'a [u8]),
    Blob(&'a [u8]),
    Null,
}

pub struct Db {
    raw: *mut RawDb,
}

// The connection is only used under the database mutex.
unsafe impl Send for Db {}

impl Db {
    pub fn open(path: &str) -> Result<Db> {
        let name = CString::new(path).map_err(|_| Error("bad path".into()))?;
        // Done automatically unless SQLite is built with SQLITE_OMIT_AUTOINIT.
        let code = unsafe { sqlite3_initialize() };
        if code != SQLITE_OK {
            return Err(Error(format!("can't initialize sqlite: code {code}")));
        }
        let mut raw = ptr::null_mut();
        let code = unsafe {
            sqlite3_open_v2(
                name.as_ptr(),
                &mut raw,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                ptr::null(),
            )
        };
        let db = Db { raw };
        if code != SQLITE_OK {
            return Err(if raw.is_null() {
                Error(format!("can't open {path}: code {code}"))
            } else {
                db.error()
            });
        }
        Ok(db)
    }

    fn error(&self) -> Error {
        let text = unsafe { CStr::from_ptr(sqlite3_errmsg(self.raw)) };
        Error(text.to_string_lossy().into_owned())
    }

    /// Runs one or more statements without parameters.
    pub fn exec(&self, sql: &str) -> Result<()> {
        let sql = CString::new(sql).map_err(|_| Error("bad sql".into()))?;
        let mut message = ptr::null_mut();
        let code = unsafe {
            sqlite3_exec(self.raw, sql.as_ptr(), ptr::null(), ptr::null_mut(), &mut message)
        };
        if code == SQLITE_OK {
            return Ok(());
        }
        if message.is_null() {
            return Err(self.error());
        }
        let text = unsafe { CStr::from_ptr(message) }.to_string_lossy().into_owned();
        unsafe { sqlite3_free(message.cast()) };
        Err(Error(text))
    }

    /// Prepares `sql` and binds `params` to ?1, ?2 and so on.
    pub fn query(&self, sql: &str, params: &[Value<'_>]) -> Result<Stmt<'_>> {
        let mut raw = ptr::null_mut();
        let code = unsafe {
            sqlite3_prepare_v2(
                self.raw,
                sql.as_ptr().cast(),
                c_int::try_from(sql.len()).map_err(|_| Error("sql too long".into()))?,
                &mut raw,
                ptr::null_mut(),
            )
        };
        if code != SQLITE_OK {
            return Err(self.error());
        }
        let stmt = Stmt { db: self, raw };
        for (index, value) in params.iter().enumerate() {
            stmt.bind(index as c_int + 1, *value)?;
        }
        Ok(stmt)
    }

    /// Runs a statement that returns no rows.
    pub fn run(&self, sql: &str, params: &[Value<'_>]) -> Result<()> {
        let mut stmt = self.query(sql, params)?;
        while stmt.next()? {}
        Ok(())
    }

    /// Whether a query returns at least one row.
    pub fn exists(&self, sql: &str, params: &[Value<'_>]) -> Result<bool> {
        self.query(sql, params)?.next()
    }
}

impl Drop for Db {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { sqlite3_close_v2(self.raw) };
        }
    }
}

pub struct Stmt<'a> {
    db: &'a Db,
    raw: *mut RawStmt,
}

impl Stmt<'_> {
    fn check(&self, code: c_int) -> Result<()> {
        if code == SQLITE_OK {
            Ok(())
        } else {
            Err(self.db.error())
        }
    }

    fn bind(&self, index: c_int, value: Value<'_>) -> Result<()> {
        // A zero length blob needs a non-null pointer, or SQLite binds NULL.
        static EMPTY: [u8; 1] = [0];
        let size = |data: &[u8]| {
            c_int::try_from(data.len()).map_err(|_| Error("value too big".into()))
        };
        let code = unsafe {
            match value {
                Value::Int(v) => sqlite3_bind_int64(self.raw, index, v),
                Value::Null => sqlite3_bind_null(self.raw, index),
                Value::Text(data) => sqlite3_bind_text(
                    self.raw,
                    index,
                    if data.is_empty() { EMPTY.as_ptr() } else { data.as_ptr() }.cast(),
                    size(data)?,
                    SQLITE_TRANSIENT,
                ),
                Value::Blob(data) => sqlite3_bind_blob(
                    self.raw,
                    index,
                    if data.is_empty() { EMPTY.as_ptr() } else { data.as_ptr() }.cast(),
                    size(data)?,
                    SQLITE_TRANSIENT,
                ),
            }
        };
        self.check(code)
    }

    /// Moves to the next row, false once there are none.
    pub fn next(&mut self) -> Result<bool> {
        match unsafe { sqlite3_step(self.raw) } {
            SQLITE_ROW => Ok(true),
            SQLITE_DONE => Ok(false),
            _ => Err(self.db.error()),
        }
    }

    pub fn int(&self, column: c_int) -> i64 {
        unsafe { sqlite3_column_int64(self.raw, column) }
    }

    pub fn optional_int(&self, column: c_int) -> Option<i64> {
        if unsafe { sqlite3_column_type(self.raw, column) } == SQLITE_NULL {
            None
        } else {
            Some(self.int(column))
        }
    }

    /// Text or blob contents, valid until the next step. NULL reads as empty.
    pub fn bytes(&self, column: c_int) -> &[u8] {
        unsafe {
            let data = sqlite3_column_blob(self.raw, column);
            let size = sqlite3_column_bytes(self.raw, column);
            if data.is_null() || size <= 0 {
                &[]
            } else {
                std::slice::from_raw_parts(data.cast(), size as usize)
            }
        }
    }
}

impl Drop for Stmt<'_> {
    fn drop(&mut self) {
        unsafe { sqlite3_finalize(self.raw) };
    }
}
