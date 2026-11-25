//! A small safe wrapper over the SQLite C API.
//!
//! SQLite itself comes from libsqlite3-sys, built by cargo from its bundled
//! amalgamation, so neither the client nor this crate keeps a copy of it.

use std::ffi::{c_int, CStr, CString};
use std::fmt;
use std::ptr;

use libsqlite3_sys as ffi;
use ffi::{SQLITE_DONE, SQLITE_NULL, SQLITE_OK, SQLITE_OPEN_CREATE, SQLITE_OPEN_READWRITE, SQLITE_ROW};

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
    raw: *mut ffi::sqlite3,
}

// The connection is only used under the database mutex.
unsafe impl Send for Db {}

impl Db {
    pub fn open(path: &str) -> Result<Db> {
        let name = CString::new(path).map_err(|_| Error("bad path".into()))?;
        // Done automatically unless SQLite is built with SQLITE_OMIT_AUTOINIT.
        let code = unsafe { ffi::sqlite3_initialize() };
        if code != SQLITE_OK {
            return Err(Error(format!("can't initialize sqlite: code {code}")));
        }
        let mut raw = ptr::null_mut();
        let code = unsafe {
            ffi::sqlite3_open_v2(
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
        let text = unsafe { CStr::from_ptr(ffi::sqlite3_errmsg(self.raw)) };
        Error(text.to_string_lossy().into_owned())
    }

    /// Runs one or more statements without parameters.
    pub fn exec(&self, sql: &str) -> Result<()> {
        let sql = CString::new(sql).map_err(|_| Error("bad sql".into()))?;
        let mut message = ptr::null_mut();
        let code = unsafe {
            ffi::sqlite3_exec(self.raw, sql.as_ptr(), None, ptr::null_mut(), &mut message)
        };
        if code == SQLITE_OK {
            return Ok(());
        }
        if message.is_null() {
            return Err(self.error());
        }
        let text = unsafe { CStr::from_ptr(message) }.to_string_lossy().into_owned();
        unsafe { ffi::sqlite3_free(message.cast()) };
        Err(Error(text))
    }

    /// Prepares `sql` and binds `params` to ?1, ?2 and so on.
    pub fn query(&self, sql: &str, params: &[Value<'_>]) -> Result<Stmt<'_>> {
        let mut raw = ptr::null_mut();
        let code = unsafe {
            ffi::sqlite3_prepare_v2(
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
            unsafe { ffi::sqlite3_close(self.raw) };
        }
    }
}

pub struct Stmt<'a> {
    db: &'a Db,
    raw: *mut ffi::sqlite3_stmt,
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
                Value::Int(v) => ffi::sqlite3_bind_int64(self.raw, index, v),
                Value::Null => ffi::sqlite3_bind_null(self.raw, index),
                Value::Text(data) => ffi::sqlite3_bind_text(
                    self.raw,
                    index,
                    if data.is_empty() { EMPTY.as_ptr() } else { data.as_ptr() }.cast(),
                    size(data)?,
                    ffi::SQLITE_TRANSIENT(),
                ),
                Value::Blob(data) => ffi::sqlite3_bind_blob(
                    self.raw,
                    index,
                    if data.is_empty() { EMPTY.as_ptr() } else { data.as_ptr() }.cast(),
                    size(data)?,
                    ffi::SQLITE_TRANSIENT(),
                ),
            }
        };
        self.check(code)
    }

    /// Moves to the next row, false once there are none.
    pub fn next(&mut self) -> Result<bool> {
        match unsafe { ffi::sqlite3_step(self.raw) } {
            SQLITE_ROW => Ok(true),
            SQLITE_DONE => Ok(false),
            _ => Err(self.db.error()),
        }
    }

    pub fn int(&self, column: c_int) -> i64 {
        unsafe { ffi::sqlite3_column_int64(self.raw, column) }
    }

    pub fn optional_int(&self, column: c_int) -> Option<i64> {
        if unsafe { ffi::sqlite3_column_type(self.raw, column) } == SQLITE_NULL {
            None
        } else {
            Some(self.int(column))
        }
    }

    /// Text or blob contents, valid until the next step. NULL reads as empty.
    pub fn bytes(&self, column: c_int) -> &[u8] {
        unsafe {
            let data = ffi::sqlite3_column_blob(self.raw, column);
            let size = ffi::sqlite3_column_bytes(self.raw, column);
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
        unsafe { ffi::sqlite3_finalize(self.raw) };
    }
}
