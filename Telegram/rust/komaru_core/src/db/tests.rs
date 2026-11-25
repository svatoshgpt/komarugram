use super::*;

fn temp_path(name: &str) -> String {
    let dir = std::env::temp_dir().join("komaru_core_tests");
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    for suffix in ["", "-shm", "-wal", "-journal"] {
        let _ = std::fs::remove_file(format!("{}{suffix}", path.display()));
    }
    path.to_string_lossy().into_owned()
}

fn message(dialog_id: i64, message_id: i32, text: &'static str) -> Message {
    let empty = Bytes::of(b"");
    Message {
        fake_id: 0,
        user_id: 7,
        dialog_id,
        grouped_id: 0,
        peer_id: dialog_id,
        from_id: 7,
        topic_id: 0,
        message_id,
        date: 1_700_000_000,
        flags: 0,
        edit_date: 0,
        views: 0,
        fwd_flags: 0,
        fwd_from_id: 0,
        fwd_name: empty,
        fwd_date: 0,
        fwd_post_author: empty,
        reply_flags: 0,
        reply_message_id: 0,
        reply_peer_id: 0,
        reply_top_id: 0,
        reply_forum_topic: 1,
        reply_serialized: Bytes::of(b"\x01\x02"),
        entity_create_date: 5,
        text: Bytes::of(text.as_bytes()),
        text_entities: empty,
        media_path: empty,
        hq_thumb_path: empty,
        document_type: 0,
        document_serialized: empty,
        thumbs_serialized: empty,
        document_attributes_serialized: empty,
        mime_type: empty,
    }
}

fn deleted(dialog_id: i64, query: &str) -> Vec<(i32, String)> {
    let mut rows = Vec::new();
    deleted_messages(7, dialog_id, 0, 0, 0, 100, query.as_bytes(), &mut |m| {
        rows.push((m.message_id, String::from_utf8(unsafe { m.text.get() }.to_vec()).unwrap()));
    });
    rows
}

fn filter_ids(query: FilterQuery) -> Vec<Vec<u8>> {
    let mut ids = Vec::new();
    filters(query, &mut |f| ids.push(unsafe { f.id.get() }.to_vec()));
    ids
}

fn schema(path: &str) -> Vec<String> {
    let db = Db::open(path).unwrap();
    let mut stmt = db
        .query("SELECT sql FROM sqlite_master WHERE sql IS NOT NULL ORDER BY name", &[])
        .unwrap();
    let mut result = Vec::new();
    while stmt.next().unwrap() {
        result.push(String::from_utf8_lossy(stmt.bytes(0)).into_owned());
    }
    result
}

// One test: the database is a process-wide global.
#[test]
fn database() {
    let path = temp_path("fresh.db");
    initialize(&path);

    // Same tables as sqlite_orm made.
    let tables = schema(&path);
    assert!(tables.contains(&r#"CREATE TABLE "RegexFilter" ("id" BLOB PRIMARY KEY NOT NULL, "text" TEXT NOT NULL, "enabled" INTEGER NOT NULL, "reversed" INTEGER NOT NULL, "caseInsensitive" INTEGER NOT NULL, "dialogId" INTEGER NULL)"#.to_string()), "{tables:#?}");
    assert!(tables.iter().any(|t| t.starts_with(r#"CREATE TABLE "DeletedMessage" ("fakeId" INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, "userId" INTEGER NOT NULL,"#)));

    add_message(MessageKind::Deleted, &message(10, 1, "hello 100%"));
    add_message(MessageKind::Deleted, &message(10, 2, "bye"));
    add_message(MessageKind::Deleted, &message(11, 3, "other chat"));
    assert_eq!(deleted(10, ""), vec![(2, "bye".into()), (1, "hello 100%".into())]);
    assert_eq!(deleted(10, "0%"), vec![(1, "hello 100%".into())]);
    assert_eq!(deleted(10, "_"), vec![]);
    assert!(has_deleted_messages(7, 11, 0));
    remove_deleted_message(7, 10, 2);
    assert_eq!(deleted(10, "").len(), 1);
    clear_deleted_messages(7, 10, 0);
    assert!(!has_deleted_messages(7, 10, 0));

    add_message(MessageKind::Edited, &message(10, 5, "v1"));
    add_message(MessageKind::Edited, &message(10, 5, "v2"));
    assert!(has_revisions(7, 10, 5));
    let mut edits = Vec::new();
    edited_messages(7, 10, 5, 0, 0, 10, &mut |m| {
        assert_eq!(unsafe { m.reply_serialized.get() }, b"\x01\x02");
        assert_eq!(m.reply_forum_topic, 1);
        edits.push(m.fake_id);
    });
    assert_eq!(edits.len(), 2);
    assert!(edits[0] > edits[1]);

    assert!(!has_filters());
    let shared = Filter {
        id: Bytes::of(b"a"),
        text: Bytes::of(b"spam"),
        enabled: 1,
        reversed: 0,
        case_insensitive: 1,
        has_dialog_id: 0,
        dialog_id: 0,
    };
    let own = Filter { id: Bytes::of(b"b"), has_dialog_id: 1, dialog_id: 10, ..shared };
    put_filter(&shared);
    put_filter(&own);
    put_filter(&shared);
    assert_eq!(filter_ids(FilterQuery::All).len(), 2);
    assert_eq!(filter_ids(FilterQuery::Shared), vec![b"a".to_vec()]);
    assert_eq!(filter_ids(FilterQuery::ForDialog(10)), vec![b"b".to_vec()]);
    assert!(has_per_dialog_filters());

    add_exclusion(11, b"a");
    assert_eq!(filter_ids(FilterQuery::ExcludedForDialog(11)), vec![b"a".to_vec()]);
    let mut excluded = 0;
    exclusions(&mut |e| {
        assert_eq!(unsafe { e.filter_id.get() }, b"a");
        excluded += 1;
    });
    assert_eq!(excluded, 1);
    delete_exclusion(11, b"a");
    assert!(filter_ids(FilterQuery::ExcludedForDialog(11)).is_empty());

    update_filter(&Filter { text: Bytes::of(b"ham"), has_dialog_id: 0, ..own });
    let mut texts = Vec::new();
    filters(FilterQuery::Shared, &mut |f| texts.push(unsafe { f.text.get() }.to_vec()));
    assert_eq!(texts.len(), 2);
    delete_filter(b"a");
    delete_all_filters();
    delete_all_exclusions();
    assert!(!has_filters());

    // A database left by an older build opens with its data.
    if let Ok(copy) = std::env::var("KOMARU_DB_COPY") {
        let path = temp_path("copy.db");
        std::fs::copy(&copy, &path).unwrap();
        let before = schema(&path);
        initialize(&path);
        assert_eq!(schema(&path), before);
        let db = Db::open(&path).unwrap();
        let mut stmt = db.query("SELECT userId, dialogId FROM DeletedMessage LIMIT 1", &[]).unwrap();
        assert!(stmt.next().unwrap());
        let (user, dialog) = (stmt.int(0), stmt.int(1));
        drop(stmt);
        let mut rows = 0;
        deleted_messages(user, dialog, 0, 0, 0, 5, b"", &mut |_| rows += 1);
        assert!(rows > 0);
    }

    // A broken file is moved aside and replaced.
    let broken = temp_path("broken.db");
    std::fs::write(&broken, b"not a database at all, just text").unwrap();
    initialize(&broken);
    assert!(!has_filters());
    assert!(schema(&broken).len() > 5);
    let moved = std::fs::read_dir(std::env::temp_dir().join("komaru_core_tests"))
        .unwrap()
        .filter_map(|e| e.ok())
        .any(|e| e.file_name().to_string_lossy().starts_with("broken_"));
    assert!(moved);
}
