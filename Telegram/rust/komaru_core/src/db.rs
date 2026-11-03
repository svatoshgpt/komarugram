//! Deleted and edited messages and the message filters, kept in
//! `tdata/ayudata.db`.
//!
//! The tables are the ones sqlite_orm created before, column for column,
//! so databases move between old and new builds untouched.

use std::path::Path;
use std::sync::Mutex;

use crate::log::log;
use crate::sqlite::{Db, Error, Result, Stmt, Value};

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Bytes {
    pub data: *const u8,
    pub size: usize,
}

impl Bytes {
    pub fn of(data: &[u8]) -> Bytes {
        Bytes { data: data.as_ptr(), size: data.len() }
    }

    /// # Safety
    /// `data` must point to `size` readable bytes, or `size` must be 0.
    pub unsafe fn get<'a>(self) -> &'a [u8] {
        if self.data.is_null() || self.size == 0 {
            &[]
        } else {
            std::slice::from_raw_parts(self.data, self.size)
        }
    }
}

/// One row of DeletedMessage or EditedMessage, same as `KomaruMessage`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Message {
    pub fake_id: i64,
    pub user_id: i64,
    pub dialog_id: i64,
    pub grouped_id: i64,
    pub peer_id: i64,
    pub from_id: i64,
    pub topic_id: i64,
    pub message_id: i32,
    pub date: i32,
    pub flags: i32,
    pub edit_date: i32,
    pub views: i32,
    pub fwd_flags: i32,
    pub fwd_from_id: i64,
    pub fwd_name: Bytes,
    pub fwd_date: i32,
    pub fwd_post_author: Bytes,
    pub reply_flags: i32,
    pub reply_message_id: i32,
    pub reply_peer_id: i64,
    pub reply_top_id: i32,
    pub reply_forum_topic: u8,
    pub reply_serialized: Bytes,
    pub entity_create_date: i32,
    pub text: Bytes,
    pub text_entities: Bytes,
    pub media_path: Bytes,
    pub hq_thumb_path: Bytes,
    pub document_type: i32,
    pub document_serialized: Bytes,
    pub thumbs_serialized: Bytes,
    pub document_attributes_serialized: Bytes,
    pub mime_type: Bytes,
}

/// One row of RegexFilter, same as `KomaruRegexFilter`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Filter {
    pub id: Bytes,
    pub text: Bytes,
    pub enabled: u8,
    pub reversed: u8,
    pub case_insensitive: u8,
    pub has_dialog_id: u8,
    pub dialog_id: i64,
}

/// One row of RegexFilterGlobalExclusion, same as `KomaruFilterExclusion`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Exclusion {
    pub fake_id: i64,
    pub dialog_id: i64,
    pub filter_id: Bytes,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum MessageKind {
    Deleted,
    Edited,
}

impl MessageKind {
    fn table(self) -> &'static str {
        match self {
            MessageKind::Deleted => "DeletedMessage",
            MessageKind::Edited => "EditedMessage",
        }
    }
}

#[derive(Clone, Copy)]
enum Type {
    Integer,
    Text,
    Blob,
}

impl Type {
    fn name(self) -> &'static str {
        match self {
            Type::Integer => "INTEGER",
            Type::Text => "TEXT",
            Type::Blob => "BLOB",
        }
    }

    fn zero(self) -> &'static str {
        match self {
            Type::Integer => "0",
            Type::Text => "''",
            Type::Blob => "X''",
        }
    }
}

struct Column {
    name: &'static str,
    kind: Type,
    nullable: bool,
    key: &'static str,
}

const fn col(name: &'static str, kind: Type) -> Column {
    Column { name, kind, nullable: false, key: "" }
}

const fn nullable(name: &'static str, kind: Type) -> Column {
    Column { name, kind, nullable: true, key: "" }
}

const fn fake_id() -> Column {
    Column {
        name: "fakeId",
        kind: Type::Integer,
        nullable: false,
        key: " PRIMARY KEY AUTOINCREMENT",
    }
}

use Type::{Blob, Integer, Text};

// Columns after fakeId, in the order of the table and of `Message`.
const MESSAGE: &[Column] = &[
    fake_id(),
    col("userId", Integer),
    col("dialogId", Integer),
    col("groupedId", Integer),
    col("peerId", Integer),
    col("fromId", Integer),
    col("topicId", Integer),
    col("messageId", Integer),
    col("date", Integer),
    col("flags", Integer),
    col("editDate", Integer),
    col("views", Integer),
    col("fwdFlags", Integer),
    col("fwdFromId", Integer),
    col("fwdName", Text),
    col("fwdDate", Integer),
    col("fwdPostAuthor", Text),
    col("replyFlags", Integer),
    col("replyMessageId", Integer),
    col("replyPeerId", Integer),
    col("replyTopId", Integer),
    col("replyForumTopic", Integer),
    col("replySerialized", Blob),
    col("entityCreateDate", Integer),
    col("text", Text),
    col("textEntities", Blob),
    col("mediaPath", Text),
    col("hqThumbPath", Text),
    col("documentType", Integer),
    col("documentSerialized", Blob),
    col("thumbsSerialized", Blob),
    col("documentAttributesSerialized", Blob),
    col("mimeType", Text),
];

const FILTER: &[Column] = &[
    Column { name: "id", kind: Blob, nullable: false, key: " PRIMARY KEY" },
    col("text", Text),
    col("enabled", Integer),
    col("reversed", Integer),
    col("caseInsensitive", Integer),
    nullable("dialogId", Integer),
];

const EXCLUSION: &[Column] = &[fake_id(), col("dialogId", Integer), col("filterId", Blob)];

const TABLES: &[(&str, &[Column])] = &[
    (
        "SchemaVersion",
        &[
            Column { name: "id", kind: Integer, nullable: false, key: " PRIMARY KEY" },
            col("version", Integer),
        ],
    ),
    ("DeletedMessage", MESSAGE),
    ("EditedMessage", MESSAGE),
    (
        "DeletedDialog",
        &[
            fake_id(),
            col("userId", Integer),
            col("dialogId", Integer),
            col("peerId", Integer),
            nullable("folderId", Integer),
            col("topMessage", Integer),
            col("lastMessageDate", Integer),
            col("flags", Integer),
            col("entityCreateDate", Integer),
        ],
    ),
    ("RegexFilter", FILTER),
    ("RegexFilterGlobalExclusion", EXCLUSION),
    (
        "SpyMessageRead",
        &[
            fake_id(),
            col("userId", Integer),
            col("dialogId", Integer),
            col("messageId", Integer),
            col("entityCreateDate", Integer),
        ],
    ),
    (
        "SpyMessageContentsRead",
        &[
            fake_id(),
            col("userId", Integer),
            col("dialogId", Integer),
            col("messageId", Integer),
            col("entityCreateDate", Integer),
        ],
    ),
];

const INDICES: &[&str] = &[
    r#"CREATE INDEX IF NOT EXISTS "idx_deleted_message_userId_dialogId_topicId_messageId" ON "DeletedMessage" ("userId", "dialogId", "topicId", "messageId")"#,
    r#"CREATE INDEX IF NOT EXISTS "idx_edited_message_userId_dialogId_messageId" ON "EditedMessage" ("userId", "dialogId", "messageId")"#,
];

const LATEST_VERSION: i64 = 1;

fn create_table_sql(table: &str, columns: &[Column]) -> String {
    let columns = columns
        .iter()
        .map(|c| {
            format!(
                "\"{}\" {}{} {}",
                c.name,
                c.kind.name(),
                c.key,
                if c.nullable { "NULL" } else { "NOT NULL" }
            )
        })
        .collect::<Vec<_>>()
        .join(", ");
    format!("CREATE TABLE IF NOT EXISTS \"{table}\" ({columns})")
}

fn column_list(columns: &[Column]) -> String {
    columns
        .iter()
        .map(|c| format!("\"{}\"", c.name))
        .collect::<Vec<_>>()
        .join(", ")
}

fn existing_columns(db: &Db, table: &str) -> Result<Vec<String>> {
    let mut stmt = db.query(&format!("PRAGMA table_info(\"{table}\")"), &[])?;
    let mut result = Vec::new();
    while stmt.next()? {
        result.push(String::from_utf8_lossy(stmt.bytes(1)).into_owned());
    }
    Ok(result)
}

/// Creates missing tables, columns and indices, like sqlite_orm's
/// sync_schema did.
fn sync_schema(db: &Db) -> Result<()> {
    for (table, columns) in TABLES {
        db.exec(&create_table_sql(table, columns))?;
        let existing = existing_columns(db, table)?;
        for column in columns.iter() {
            if existing.iter().any(|name| name == column.name) {
                continue;
            }
            log!("Database: adding column {}.{}", table, column.name);
            let constraint = if column.nullable {
                "NULL".to_string()
            } else {
                format!("NOT NULL DEFAULT {}", column.kind.zero())
            };
            db.exec(&format!(
                "ALTER TABLE \"{table}\" ADD COLUMN \"{}\" {} {constraint}",
                column.name,
                column.kind.name()
            ))?;
        }
    }
    for index in INDICES {
        db.exec(index)?;
    }
    Ok(())
}

fn migrate(db: &Db, version: i64) -> Result<()> {
    match version {
        // RegexFilter got a primary key, the old table is dropped.
        1 => db.exec("DROP TABLE IF EXISTS \"RegexFilter\""),
        _ => Ok(()),
    }
}

fn run_migrations(db: &Db) -> Result<()> {
    let current = {
        let mut stmt = db.query("SELECT \"version\" FROM \"SchemaVersion\" WHERE \"id\" = 1", &[])?;
        if stmt.next()? {
            Some(stmt.int(0))
        } else {
            None
        }
    };
    let current = match current {
        Some(version) => version,
        None => {
            db.run("INSERT INTO \"SchemaVersion\" (\"id\", \"version\") VALUES (1, 0)", &[])?;
            0
        }
    };
    if current >= LATEST_VERSION {
        log!("Database is ok");
        return Ok(());
    }
    log!("Database version: {current}. Latest version: {LATEST_VERSION}.");
    for version in current + 1..=LATEST_VERSION {
        db.exec("BEGIN")?;
        let applied = migrate(db, version).and_then(|_| {
            db.run(
                "UPDATE \"SchemaVersion\" SET \"version\" = ?1 WHERE \"id\" = 1",
                &[Value::Int(version)],
            )
        });
        match applied {
            Ok(()) => {
                db.exec("COMMIT")?;
                log!("Applied migration for version: {version}.");
            }
            Err(error) => {
                let _ = db.exec("ROLLBACK");
                return Err(Error(format!("migration to {version} failed: {error}")));
            }
        }
    }
    Ok(())
}

fn prepare(path: &str) -> Result<Db> {
    let db = Db::open(path)?;
    sync_schema(&db)?;
    run_migrations(&db)?;
    sync_schema(&db)?;
    Ok(db)
}

/// Renames a broken database out of the way, keeping it for recovery.
fn move_aside(path: &str) {
    let time = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    let base = path.strip_suffix(".db").unwrap_or(path);
    for suffix in ["", "-shm", "-wal"] {
        let from = format!("{path}{suffix}");
        if Path::new(&from).exists() {
            let to = format!("{base}_{time}.db{suffix}");
            if let Err(error) = std::fs::rename(&from, &to) {
                log!("Database: can't move {from}: {error}");
            }
        }
    }
}

static DATABASE: Mutex<Option<Db>> = Mutex::new(None);

pub fn initialize(path: &str) {
    let mut database = DATABASE.lock().unwrap_or_else(|e| e.into_inner());
    *database = None;
    let db = match prepare(path) {
        Ok(db) => db,
        Err(error) => {
            log!("Database initialization failed: {error}");
            move_aside(path);
            match prepare(path) {
                Ok(db) => db,
                Err(error) => {
                    log!("Database can't be created: {error}");
                    return;
                }
            }
        }
    };
    *database = Some(db);
}

/// Runs `f` on the open database, logging failures with `what`.
fn with<T>(what: &str, fallback: T, f: impl FnOnce(&Db) -> Result<T>) -> T {
    let database = DATABASE.lock().unwrap_or_else(|e| e.into_inner());
    let Some(db) = database.as_ref() else {
        return fallback;
    };
    match f(db) {
        Ok(value) => value,
        Err(error) => {
            log!("Failed to {what}: {error}");
            fallback
        }
    }
}

fn message_values(m: &Message) -> [Value<'_>; 32] {
    let bytes = |b: Bytes| unsafe { b.get() };
    let int = |v: i32| Value::Int(v.into());
    [
        Value::Int(m.user_id),
        Value::Int(m.dialog_id),
        Value::Int(m.grouped_id),
        Value::Int(m.peer_id),
        Value::Int(m.from_id),
        Value::Int(m.topic_id),
        int(m.message_id),
        int(m.date),
        int(m.flags),
        int(m.edit_date),
        int(m.views),
        int(m.fwd_flags),
        Value::Int(m.fwd_from_id),
        Value::Text(bytes(m.fwd_name)),
        int(m.fwd_date),
        Value::Text(bytes(m.fwd_post_author)),
        int(m.reply_flags),
        int(m.reply_message_id),
        Value::Int(m.reply_peer_id),
        int(m.reply_top_id),
        Value::Int(m.reply_forum_topic.into()),
        Value::Blob(bytes(m.reply_serialized)),
        int(m.entity_create_date),
        Value::Text(bytes(m.text)),
        Value::Blob(bytes(m.text_entities)),
        Value::Text(bytes(m.media_path)),
        Value::Text(bytes(m.hq_thumb_path)),
        int(m.document_type),
        Value::Blob(bytes(m.document_serialized)),
        Value::Blob(bytes(m.thumbs_serialized)),
        Value::Blob(bytes(m.document_attributes_serialized)),
        Value::Text(bytes(m.mime_type)),
    ]
}

/// The row under `stmt`, selected with all MESSAGE columns.
fn read_message(stmt: &Stmt<'_>) -> Message {
    let int = |c| stmt.int(c) as i32;
    let bytes = |c| Bytes::of(stmt.bytes(c));
    Message {
        fake_id: stmt.int(0),
        user_id: stmt.int(1),
        dialog_id: stmt.int(2),
        grouped_id: stmt.int(3),
        peer_id: stmt.int(4),
        from_id: stmt.int(5),
        topic_id: stmt.int(6),
        message_id: int(7),
        date: int(8),
        flags: int(9),
        edit_date: int(10),
        views: int(11),
        fwd_flags: int(12),
        fwd_from_id: stmt.int(13),
        fwd_name: bytes(14),
        fwd_date: int(15),
        fwd_post_author: bytes(16),
        reply_flags: int(17),
        reply_message_id: int(18),
        reply_peer_id: stmt.int(19),
        reply_top_id: int(20),
        reply_forum_topic: u8::from(stmt.int(21) != 0),
        reply_serialized: bytes(22),
        entity_create_date: int(23),
        text: bytes(24),
        text_entities: bytes(25),
        media_path: bytes(26),
        hq_thumb_path: bytes(27),
        document_type: int(28),
        document_serialized: bytes(29),
        thumbs_serialized: bytes(30),
        document_attributes_serialized: bytes(31),
        mime_type: bytes(32),
    }
}

pub fn add_message(kind: MessageKind, message: &Message) {
    let what = match kind {
        MessageKind::Deleted => "save deleted message",
        MessageKind::Edited => "save edited message",
    };
    with(what, (), |db| {
        let columns = &MESSAGE[1..];
        let marks = vec!["?"; columns.len()].join(", ");
        db.run(
            &format!(
                "INSERT INTO \"{}\" ({}) VALUES ({marks})",
                kind.table(),
                column_list(columns)
            ),
            &message_values(message),
        )
    })
}

fn each_message(
    db: &Db,
    sql: &str,
    params: &[Value<'_>],
    sink: &mut dyn FnMut(&Message),
) -> Result<()> {
    let mut stmt = db.query(sql, params)?;
    while stmt.next()? {
        sink(&read_message(&stmt));
    }
    Ok(())
}

pub fn edited_messages(
    user_id: i64,
    dialog_id: i64,
    message_id: i64,
    min_id: i64,
    max_id: i64,
    limit: i64,
    sink: &mut dyn FnMut(&Message),
) {
    with("get edited messages", (), |db| {
        each_message(
            db,
            &format!(
                "SELECT {} FROM \"EditedMessage\" WHERE \"userId\" = ?1 AND \"dialogId\" = ?2 \
                 AND \"messageId\" = ?3 AND (\"fakeId\" > ?4 OR ?4 = 0) \
                 AND (\"fakeId\" < ?5 OR ?5 = 0) ORDER BY \"fakeId\" DESC LIMIT ?6",
                column_list(MESSAGE)
            ),
            &[
                Value::Int(user_id),
                Value::Int(dialog_id),
                Value::Int(message_id),
                Value::Int(min_id),
                Value::Int(max_id),
                Value::Int(limit),
            ],
            sink,
        )
    })
}

pub fn has_revisions(user_id: i64, dialog_id: i64, message_id: i64) -> bool {
    with("check message revisions", false, |db| {
        db.exists(
            "SELECT 1 FROM \"EditedMessage\" WHERE \"userId\" = ?1 AND \"dialogId\" = ?2 \
             AND \"messageId\" = ?3 LIMIT 1",
            &[Value::Int(user_id), Value::Int(dialog_id), Value::Int(message_id)],
        )
    })
}

/// LIKE pattern matching `query` anywhere, with `\` as the escape.
fn like_pattern(query: &[u8]) -> Vec<u8> {
    let mut result = Vec::with_capacity(query.len() + 2);
    result.push(b'%');
    for &c in query {
        if matches!(c, b'%' | b'_' | b'\\') {
            result.push(b'\\');
        }
        result.push(c);
    }
    result.push(b'%');
    result
}

#[allow(clippy::too_many_arguments)]
pub fn deleted_messages(
    user_id: i64,
    dialog_id: i64,
    topic_id: i64,
    min_id: i64,
    max_id: i64,
    limit: i64,
    query: &[u8],
    sink: &mut dyn FnMut(&Message),
) {
    with("get deleted messages", (), |db| {
        let pattern = like_pattern(query);
        let search = if query.is_empty() {
            ""
        } else {
            " AND \"text\" LIKE ?7 ESCAPE '\\'"
        };
        let mut params = vec![
            Value::Int(user_id),
            Value::Int(dialog_id),
            Value::Int(topic_id),
            Value::Int(min_id),
            Value::Int(max_id),
            Value::Int(limit),
        ];
        if !query.is_empty() {
            params.push(Value::Text(&pattern));
        }
        each_message(
            db,
            &format!(
                "SELECT {} FROM \"DeletedMessage\" WHERE \"userId\" = ?1 AND \"dialogId\" = ?2 \
                 AND (\"topicId\" = ?3 OR ?3 = 0) AND (\"messageId\" > ?4 OR ?4 = 0) \
                 AND (\"messageId\" < ?5 OR ?5 = 0){search} \
                 ORDER BY \"messageId\" DESC LIMIT ?6",
                column_list(MESSAGE)
            ),
            &params,
            sink,
        )
    })
}

const DELETED_IN_DIALOG: &str = "\"userId\" = ?1 AND \"dialogId\" = ?2 \
    AND (\"topicId\" = ?3 OR ?3 = 0)";

pub fn has_deleted_messages(user_id: i64, dialog_id: i64, topic_id: i64) -> bool {
    with("check deleted messages", false, |db| {
        db.exists(
            &format!("SELECT 1 FROM \"DeletedMessage\" WHERE {DELETED_IN_DIALOG} LIMIT 1"),
            &[Value::Int(user_id), Value::Int(dialog_id), Value::Int(topic_id)],
        )
    })
}

pub fn remove_deleted_message(user_id: i64, dialog_id: i64, message_id: i64) {
    with("remove deleted message", (), |db| {
        db.run(
            "DELETE FROM \"DeletedMessage\" WHERE \"userId\" = ?1 AND \"dialogId\" = ?2 \
             AND \"messageId\" = ?3",
            &[Value::Int(user_id), Value::Int(dialog_id), Value::Int(message_id)],
        )
    })
}

pub fn clear_deleted_messages(user_id: i64, dialog_id: i64, topic_id: i64) {
    with("clear deleted messages", (), |db| {
        db.run(
            &format!("DELETE FROM \"DeletedMessage\" WHERE {DELETED_IN_DIALOG}"),
            &[Value::Int(user_id), Value::Int(dialog_id), Value::Int(topic_id)],
        )
    })
}

/// Which filters to list.
#[derive(Clone, Copy)]
pub enum FilterQuery {
    All,
    /// Filters for every chat.
    Shared,
    /// Filters of one chat.
    ForDialog(i64),
    /// Shared filters turned off for one chat.
    ExcludedForDialog(i64),
}

pub fn filters(query: FilterQuery, sink: &mut dyn FnMut(&Filter)) {
    with("get filters", (), |db| {
        let (condition, params) = match query {
            FilterQuery::All => ("", vec![]),
            FilterQuery::Shared => (" WHERE \"dialogId\" IS NULL", vec![]),
            FilterQuery::ForDialog(id) => (" WHERE \"dialogId\" = ?1", vec![Value::Int(id)]),
            FilterQuery::ExcludedForDialog(id) => (
                " WHERE \"id\" IN (SELECT \"filterId\" FROM \"RegexFilterGlobalExclusion\" \
                 WHERE \"dialogId\" = ?1)",
                vec![Value::Int(id)],
            ),
        };
        let mut stmt = db.query(
            &format!("SELECT {} FROM \"RegexFilter\"{condition}", column_list(FILTER)),
            &params,
        )?;
        while stmt.next()? {
            let dialog_id = stmt.optional_int(5);
            sink(&Filter {
                id: Bytes::of(stmt.bytes(0)),
                text: Bytes::of(stmt.bytes(1)),
                enabled: u8::from(stmt.int(2) != 0),
                reversed: u8::from(stmt.int(3) != 0),
                case_insensitive: u8::from(stmt.int(4) != 0),
                has_dialog_id: u8::from(dialog_id.is_some()),
                dialog_id: dialog_id.unwrap_or(0),
            });
        }
        Ok(())
    })
}

pub fn exclusions(sink: &mut dyn FnMut(&Exclusion)) {
    with("get filter exclusions", (), |db| {
        let mut stmt = db.query(
            &format!("SELECT {} FROM \"RegexFilterGlobalExclusion\"", column_list(EXCLUSION)),
            &[],
        )?;
        while stmt.next()? {
            sink(&Exclusion {
                fake_id: stmt.int(0),
                dialog_id: stmt.int(1),
                filter_id: Bytes::of(stmt.bytes(2)),
            });
        }
        Ok(())
    })
}

fn filter_values(filter: &Filter) -> [Value<'_>; 6] {
    [
        Value::Blob(unsafe { filter.id.get() }),
        Value::Text(unsafe { filter.text.get() }),
        Value::Int(filter.enabled.into()),
        Value::Int(filter.reversed.into()),
        Value::Int(filter.case_insensitive.into()),
        if filter.has_dialog_id != 0 {
            Value::Int(filter.dialog_id)
        } else {
            Value::Null
        },
    ]
}

/// Adds the filter, replacing one with the same id.
pub fn put_filter(filter: &Filter) {
    with("save filter", (), |db| {
        db.run(
            &format!(
                "REPLACE INTO \"RegexFilter\" ({}) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                column_list(FILTER)
            ),
            &filter_values(filter),
        )
    })
}

pub fn update_filter(filter: &Filter) {
    with("update filter", (), |db| {
        db.run(
            "UPDATE \"RegexFilter\" SET \"text\" = ?2, \"enabled\" = ?3, \"reversed\" = ?4, \
             \"caseInsensitive\" = ?5, \"dialogId\" = ?6 WHERE \"id\" = ?1",
            &filter_values(filter),
        )
    })
}

pub fn delete_filter(id: &[u8]) {
    with("delete filter", (), |db| {
        db.run("DELETE FROM \"RegexFilter\" WHERE \"id\" = ?1", &[Value::Blob(id)])
    })
}

pub fn add_exclusion(dialog_id: i64, filter_id: &[u8]) {
    with("save filter exclusion", (), |db| {
        db.run(
            "INSERT INTO \"RegexFilterGlobalExclusion\" (\"dialogId\", \"filterId\") VALUES (?1, ?2)",
            &[Value::Int(dialog_id), Value::Blob(filter_id)],
        )
    })
}

pub fn delete_exclusions_of_filter(filter_id: &[u8]) {
    with("delete filter exclusions", (), |db| {
        db.run(
            "DELETE FROM \"RegexFilterGlobalExclusion\" WHERE \"filterId\" = ?1",
            &[Value::Blob(filter_id)],
        )
    })
}

pub fn delete_exclusion(dialog_id: i64, filter_id: &[u8]) {
    with("delete filter exclusion", (), |db| {
        db.run(
            "DELETE FROM \"RegexFilterGlobalExclusion\" WHERE \"filterId\" = ?1 AND \"dialogId\" = ?2",
            &[Value::Blob(filter_id), Value::Int(dialog_id)],
        )
    })
}

pub fn delete_all_filters() {
    with("delete all filters", (), |db| db.run("DELETE FROM \"RegexFilter\"", &[]))
}

pub fn delete_all_exclusions() {
    with("delete all filter exclusions", (), |db| {
        db.run("DELETE FROM \"RegexFilterGlobalExclusion\"", &[])
    })
}

pub fn has_filters() -> bool {
    with("check filters", false, |db| {
        db.exists("SELECT 1 FROM \"RegexFilter\" LIMIT 1", &[])
    })
}

pub fn has_per_dialog_filters() -> bool {
    with("check filters", false, |db| {
        Ok(db.exists("SELECT 1 FROM \"RegexFilter\" WHERE \"dialogId\" IS NOT NULL LIMIT 1", &[])?
            || db.exists("SELECT 1 FROM \"RegexFilterGlobalExclusion\" LIMIT 1", &[])?)
    })
}

#[cfg(test)]
mod tests;
