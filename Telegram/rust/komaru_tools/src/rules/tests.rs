use std::path::{Path, PathBuf};
use std::process::Command;

use serde_json::json;

use super::{edit_hook, staged};

const LICENSE: &str = "/*\nThis file is part of Telegram Desktop,\nthe official desktop \
    application for the Telegram messaging service.\n\nFor license and copyright \
    information please follow this link:\n\
    https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL\n*/\n";

fn run(payload: serde_json::Value) -> (u8, String) {
    let mut out = vec![];
    let code = edit_hook(&payload.to_string(), &mut out);
    (code, out.join("\n"))
}

fn edit_at(new: &str, old: &str, path: &str) -> (u8, String) {
    run(json!({"tool_name": "Edit", "tool_input": {
        "file_path": path, "old_string": old, "new_string": new}}))
}

fn edit(new: &str) -> (u8, String) {
    edit_at(new, "x", "/tmp/rules/foo.cpp")
}

fn write(content: &str, path: &Path) -> (u8, String) {
    run(json!({"tool_name": "Write", "tool_input": {
        "file_path": path.to_string_lossy(), "content": content}}))
}

#[test]
fn one_line_comment_passes() {
    assert_eq!(edit("// the server rejects more\n++count;\n").0, 0);
}

#[test]
fn two_line_comment_fails() {
    let (code, why) = edit("// the server rejects more,\n// so we stop\nf();\n");
    assert_eq!(code, 2);
    assert!(why.contains("2-line comment: a comment is one line"));
    assert!(why.contains("reference URL"));
}

#[test]
fn url_blocks() {
    assert_eq!(edit("// the scheme, see\n// https://core.telegram.org\nf();\n").0, 0);
    assert_eq!(edit("// a\n// b\n// https://core.telegram.org\n// c\n// d\nf();\n").0, 0);
    assert_eq!(edit("// WHY: a\n// b\nf();\n").0, 2);
}

#[test]
fn comment_shapes() {
    assert_eq!(edit("//foo();\n//bar();\n//baz();\n").0, 2);
    assert_eq!(edit("/* the server\n   rejects more */\nf();\n").0, 2);
    assert_eq!(edit("/* rejected */ f();\n").0, 0);
    assert_eq!(edit("f(); /* the server\nrejects more */\n").0, 2);
}

#[test]
fn edit_has_no_budget() {
    let many: String = (0..6).map(|i| format!("a{i} = {i}; // set a{i}\n")).collect();
    assert_eq!(edit(&many).0, 0);
    assert_eq!(edit("// one\nf();\n// two\ng();\n").0, 0);
}

#[test]
fn moved_block_is_not_new() {
    let block = "// the server\n// rejects more\n";
    let new = format!("{block}g();\n");
    let old = format!("{block}f();\n");
    assert_eq!(edit_at(&new, &old, "/tmp/rules/foo.cpp").0, 0);
}

#[test]
fn harness_is_exempt_from_comment_rules() {
    let path = "/tmp/Telegram/SourceFiles/test/harness.cpp";
    assert_eq!(edit_at("// a\n// b\n// c\n// d\n", "x", path).0, 0);
    assert_eq!(edit_at("qMin(a, b);\n", "x", path).0, 2);
}

#[test]
fn banned_functions() {
    let (code, why) = edit("w = qMin(a, b);\n");
    assert_eq!(code, 2);
    assert!(why.contains("qMin is banned in this repository -- write std::min"));
    assert!(edit("w = qBound(0, v, 9);\n").1.contains("std::clamp"));
    assert!(edit("w = qAbs (v);\n").1.contains("std::abs"));
    assert_eq!(edit("w = myqMin(a, b) + qMinimum(c);\n").0, 0);
    assert_eq!(edit("// qMin(a, b) was here\n").0, 0);
    assert_eq!(edit("log(\"qMin(a, b)\");\n").0, 0);
    assert_eq!(edit("u = \"http://x\"; v = qMin(a, b);\n").0, 2);
    let old = "w = qMin(a, b) + 1;\n";
    assert_eq!(edit_at("w = qMin(a, b) + 2;\n", old, "/tmp/rules/foo.cpp").0, 2);
}

#[test]
fn other_payloads() {
    assert_eq!(edit_at("// a\n// b\n// c\n", "x", "/tmp/x.py").0, 0);
    assert_eq!(edit_at("qMin(a, b)\n", "x", "/tmp/x.style").0, 0);
    assert_eq!(edit_hook("not json", &mut vec![]), 0);
    let (code, _) = run(json!({"tool_name": "Edit", "tool_input": {
        "file_path": "/tmp/rules/foo.cpp",
        "edits": [{"old_string": "x", "new_string": "qMin(a, b);\n"}]}}));
    assert_eq!(code, 2);
}

struct Repository(PathBuf);

impl Repository {
    fn new(name: &str) -> Repository {
        let path = std::env::temp_dir().join(format!("komaru_rules_{name}_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir_all(&path).unwrap();
        let repo = Repository(path);
        repo.git(&["init", "-q"]);
        repo.git(&["config", "user.email", "t@t"]);
        repo.git(&["config", "user.name", "t"]);
        repo.git(&["config", "core.autocrlf", "false"]);
        // Machine-wide hooks have no say in this disposable repository.
        std::fs::create_dir_all(repo.0.join("hooks")).unwrap();
        repo.git(&["config", "core.hooksPath", "hooks"]);
        repo.commit(&[("a.cpp", "int a() {\n\treturn 1;\n}\n")]);
        repo
    }

    fn git(&self, args: &[&str]) {
        let done = Command::new("git").args(args).current_dir(&self.0).output().unwrap();
        assert!(done.status.success(), "{}", String::from_utf8_lossy(&done.stderr));
    }

    fn put(&self, files: &[(&str, &str)]) {
        for (name, content) in files {
            let full = self.0.join(name);
            std::fs::create_dir_all(full.parent().unwrap()).unwrap();
            std::fs::write(&full, content).unwrap();
            self.git(&["add", name]);
        }
    }

    fn commit(&self, files: &[(&str, &str)]) {
        self.put(files);
        self.git(&["commit", "-q", "-m", "base"]);
    }

    fn stage(&self, files: &[(&str, &str)]) -> (u8, String) {
        self.put(files);
        let mut out = vec![];
        let code = staged(Some(&self.0), &mut out).unwrap();
        (code, out.join("\n"))
    }
}

impl Drop for Repository {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

#[test]
fn staged_budget() {
    let repo = Repository::new("budget");
    let (code, why) = repo.stage(&[("a.cpp", "// one\nint a() {\n\treturn 1; // two\n}\n")]);
    assert_eq!(code, 0, "{why}");
    let (code, why) =
        repo.stage(&[("a.cpp", "// one\nint a() {\n\t// two\n\treturn 1;\n}\n// three\n")]);
    assert_eq!(code, 1);
    assert!(why.contains("adds 3 comment lines; 2 is the budget"));
    let (code, why) = repo.stage(&[(
        "a.cpp",
        "int a() {\n\tint b = 1; // b\n\tint c = 2; // c\n\treturn b + c; // sum\n}\n",
    )]);
    assert_eq!(code, 1);
    assert!(why.contains("adds 3 comment lines"));
}

#[test]
fn staged_free_labels() {
    let repo = Repository::new("labels");
    let (code, why) = repo.stage(&[(
        "a.cpp",
        "#ifndef A\n#define A\nnamespace A {\nint a() {\n\treturn 1; // one\n}\n\
         } // namespace A\n#endif // A\n// two\n",
    )]);
    assert_eq!(code, 0, "{why}");
}

#[test]
fn staged_moved_comment_is_free() {
    let repo = Repository::new("moved");
    repo.commit(&[("a.cpp", "// one\n// two\n// three\nint a() {\n\treturn 1;\n}\n")]);
    let (code, why) = repo.stage(&[
        ("a.cpp", "int a() {\n\treturn 1;\n}\n"),
        ("b.cpp", "// one\n// two\n// three\nint b() {\n\treturn 2;\n}\n"),
    ]);
    assert_eq!(code, 0, "{why}");
}

#[test]
fn staged_edited_line_keeps_its_trailing_comment() {
    let repo = Repository::new("trailing");
    repo.commit(&[("a.cpp", "int a() {\n\treturn 1; // the server\n}\n")]);
    let (code, why) =
        repo.stage(&[("a.cpp", "int a() {\n\treturn 2; // the server\n}\n// one\n// two\n")]);
    assert_eq!(code, 1);
    assert!(why.contains("a.cpp:4: a comment is one line"));
    assert!(!why.contains("budget"));
}

#[test]
fn staged_url_block_is_free() {
    let repo = Repository::new("url");
    let (code, why) = repo.stage(&[(
        "a.cpp",
        "// one\n\n// two\n\n// the docs\n// https://core.telegram.org\nint a() {\n\treturn 1;\n}\n",
    )]);
    assert_eq!(code, 0, "{why}");
    let (code, why) = repo.stage(&[(
        "a.cpp",
        "// WHY: a\n// b\nint a() {\n\treturn 1;\n}\n",
    )]);
    assert_eq!(code, 1);
    assert!(why.contains("a.cpp:1: a comment is one line"));
}

#[test]
fn staged_named_by_line() {
    let repo = Repository::new("lines");
    let (code, why) = repo.stage(&[("a.cpp", "int a() {\n\treturn 1;\n}\n\n// one\n// two\n")]);
    assert_eq!(code, 1);
    assert!(why.contains("a.cpp:5: a comment is one line"));
    let (code, why) = repo.stage(&[("a.cpp", "int a() {\n\treturn qMax(1, 2);\n}\n")]);
    assert_eq!(code, 1);
    assert!(why.contains("a.cpp:2: qMax is banned -- write std::max"));
}

#[test]
fn staged_exemptions() {
    let repo = Repository::new("exempt");
    let (code, why) = repo.stage(&[(
        "Telegram/SourceFiles/test/t.cpp",
        "// a\n// b\n// c\n// d\nint t() {\n\treturn 1;\n}\n",
    )]);
    assert_eq!(code, 0, "{why}");
    let (code, why) = repo.stage(&[
        ("t.py", "# a\n# b\n// c\n// d\nqMin(1, 2)\n"),
        ("a.style", "// a\n// b\nx: 1px;\n"),
    ]);
    assert_eq!(code, 0, "{why}");
    let license = format!("{LICENSE}int b() {{\n\treturn 2;\n}}\n");
    let (code, why) = repo.stage(&[("b.cpp", &license)]);
    assert_eq!(code, 0, "{why}");
}

#[test]
fn write_of_new_file_with_license() {
    let dir = std::env::temp_dir().join(format!("komaru_rules_write_{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("new.cpp");
    let (code, why) = write(&format!("{LICENSE}#include \"a.h\"\n\nf();\n"), &path);
    assert_eq!(code, 0, "{why}");
    assert_eq!(write(&format!("{LICENSE}// a\n// b\nf();\n"), &path).0, 2);
    let (code, why) = write(&format!("{LICENSE}// a\nf();\n"), &path);
    assert_eq!(code, 0, "{why}");
    let _ = std::fs::remove_dir_all(&dir);
}
