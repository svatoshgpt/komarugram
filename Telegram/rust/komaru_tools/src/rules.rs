//! Refuses what the house rules ban: as an edit hook, or `--staged`.

use std::collections::{BTreeMap, HashSet};
use std::io::Read;
use std::path::Path;
use std::process::Command;

use regex::{Captures, Regex};
use serde_json::Value;

const SOURCES: &[&str] = &[".cpp", ".h", ".mm", ".cxx"];
const LICENSE: &str = "This file is part of ";
const HARNESS: &str = "/SourceFiles/test/";
const MARKER: &str = "// WHY:";
const MARKED_LINES: usize = 3;
const PLAIN_LINES: usize = 2;
const MARKED_BLOCKS: usize = 1;

pub fn run(list: &[String]) -> Result<u8, String> {
    let mut out = vec![];
    let code = if list.iter().any(|a| a == "--staged") {
        staged(None, &mut out)?
    } else {
        let mut input = String::new();
        let _ = std::io::stdin().read_to_string(&mut input);
        edit_hook(&input, &mut out)
    };
    for line in out {
        eprintln!("{line}");
    }
    Ok(code)
}

fn edit_hook(input: &str, out: &mut Vec<String>) -> u8 {
    let Ok(payload) = serde_json::from_str::<Value>(input) else {
        return 0;
    };
    let path = payload["tool_response"]["filePath"]
        .as_str()
        .or(payload["tool_input"]["file_path"].as_str())
        .unwrap_or("");
    if is_source(path) { check_edit(&payload, path, out) } else { 0 }
}

fn is_source(path: &str) -> bool {
    SOURCES.iter().any(|s| path.ends_with(s))
}

/// The repository root: the tool lives in Telegram/rust/komaru_tools.
fn root() -> &'static Path {
    Path::new(concat!(env!("CARGO_MANIFEST_DIR"), "/../../.."))
}

/// Name and replacement of every entry in CustomFunctions.
fn banned() -> Vec<(String, String)> {
    let text = std::fs::read_to_string(root().join(".clang-tidy")).unwrap_or_default();
    Regex::new(r"\^::(\w+)\$,([^,;]+),")
        .unwrap()
        .captures_iter(&text)
        .map(|c| (c[1].to_string(), c[2].to_string()))
        .collect()
}

fn calls(name: &str) -> Regex {
    Regex::new(&format!(r"\b{name}\s*\(")).unwrap()
}

/// The literal's quotes around filler, so byte offsets stay put.
fn hollow(c: &Captures) -> String {
    let quoted = &c[0];
    let quote = &quoted[..1];
    format!("{quote}{}{quote}", "_".repeat(quoted.len() - 2))
}

fn blanked(line: &str) -> String {
    let line = Regex::new(r#""(?:[^"\\]|\\.)*""#).unwrap().replace_all(line, hollow);
    Regex::new(r"'(?:[^'\\]|\\.)*'").unwrap().replace_all(&line, hollow).into_owned()
}

fn without_comments(text: &str) -> String {
    let text: Vec<String> = text.split('\n').map(blanked).collect();
    let text = Regex::new(r"//[^\n]*").unwrap().replace_all(&text.join("\n"), "").into_owned();
    Regex::new(r"(?s)/\*.*?\*/").unwrap().replace_all(&text, "").into_owned()
}

/// Offset of the first comment marker outside literals.
fn comment_start(line: &str) -> Option<usize> {
    let body = blanked(line);
    [body.find("//"), body.find("/*")].into_iter().flatten().min()
}

/// What a moved comment keeps: its text, not the code beside it.
fn comment_text(line: &str) -> &str {
    match comment_start(line) {
        Some(at) => line[at..].trim(),
        None => line.trim(),
    }
}

#[derive(Clone, Copy, PartialEq)]
enum Kind {
    Full,
    Tail,
}

/// The line's kind and whether a /* */ block is still open after it.
fn classify(line: &str, inside: bool) -> (Option<Kind>, bool) {
    let body = blanked(line);
    if inside {
        return (Some(Kind::Full), !body.contains("*/"));
    }
    let Some(at) = comment_start(line) else {
        return (None, false);
    };
    let opens = body[at..].starts_with("/*") && !body[at + 2..].contains("*/");
    let code = body[..at].trim();
    if code == "}" || code.starts_with('#') {
        return (None, opens); // a closer or directive may carry its label
    }
    (Some(if code.is_empty() { Kind::Full } else { Kind::Tail }), opens)
}

/// Comment blocks as (line number, lines): runs of whole-line comments,
/// and every trailing comment on its own.
fn blocks(numbered: &[(usize, String)]) -> Vec<(usize, Vec<String>)> {
    let mut out = vec![];
    let mut run: Vec<String> = vec![];
    let (mut first, mut previous, mut inside) = (0, None::<usize>, false);
    for (number, line) in numbered {
        let number = *number;
        let gap = previous.is_some_and(|p| number != p + 1);
        let was = inside && !gap;
        let (kind, now) = classify(line, was);
        inside = now;
        if !run.is_empty() && (gap || kind != Some(Kind::Full)) {
            out.push((first, std::mem::take(&mut run)));
        }
        if kind == Some(Kind::Full) || (kind == Some(Kind::Tail) && inside) {
            if run.is_empty() {
                first = number;
            }
            run.push(line.clone());
            if was && !inside {
                out.push((first, std::mem::take(&mut run))); // a closed /* */ ends its block
            }
        } else if kind == Some(Kind::Tail) {
            out.push((number, vec![line.clone()]));
        }
        previous = Some(number);
    }
    if !run.is_empty() {
        out.push((first, run));
    }
    out
}

/// Why this comment block breaks the rule.
fn fault(block: &[String]) -> Option<String> {
    if block.iter().any(|l| l.contains(LICENSE)) {
        return None;
    }
    if block[0].trim().starts_with(MARKER) {
        return (block.len() > MARKED_LINES)
            .then(|| format!("a {MARKER} block runs {MARKED_LINES} lines at most"));
    }
    (block.len() > 1).then(|| {
        format!("a comment is one line; up to {MARKED_LINES} only when it opens with {MARKER}")
    })
}

fn strings(tool: &Value, key: &str) -> Vec<String> {
    let mut parts = vec![tool[key].as_str().unwrap_or("").to_string()];
    if let Some(edits) = tool["edits"].as_array() {
        parts.extend(edits.iter().map(|e| e[key].as_str().unwrap_or("").to_string()));
    }
    parts
}

/// The text this call put into the file, across all the edit shapes.
fn written(payload: &Value) -> String {
    let tool = &payload["tool_input"];
    let mut parts = strings(tool, "new_string");
    parts.insert(1, tool["content"].as_str().unwrap_or("").to_string());
    parts.join("\n")
}

/// What was there before, so moved code is not read as freshly written.
fn replaced(payload: &Value, path: &str) -> String {
    let tool = &payload["tool_input"];
    let mut parts = strings(tool, "old_string");
    if tool["content"].as_str().is_some_and(|c| !c.is_empty()) {
        // Relative to the file, so a submodule answers for its own files.
        let path = Path::new(path);
        let name = path.file_name().map(|n| n.to_string_lossy()).unwrap_or_default();
        let dir = path.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
        let shown = Command::new("git")
            .arg("show")
            .arg(format!("HEAD:./{name}"))
            .current_dir(dir)
            .output();
        if let Ok(shown) = shown {
            parts.push(String::from_utf8_lossy(&shown.stdout).into_owned());
        }
    }
    parts.join("\n")
}

fn check_edit(payload: &Value, path: &str, out: &mut Vec<String>) -> u8 {
    let text = written(payload);
    let code = without_comments(&text);
    let mut bad = 0;
    for (name, into) in banned() {
        if calls(&name).is_match(&code) {
            out.push(format!("{name} is banned in this repository -- write {into} instead."));
            bad += 1;
        }
    }
    if !path.replace('\\', "/").contains(HARNESS) {
        let was = replaced(payload, path);
        let numbered: Vec<_> = text.split('\n').map(String::from).enumerate().collect();
        for (_, block) in blocks(&numbered) {
            if let Some(why) = fault(&block) {
                if !was.contains(&block.join("\n")) {
                    out.push(format!("{}-line comment: {why}.", block.len()));
                    bad += 1;
                }
            }
        }
    }
    if bad == 0 {
        return 0;
    }
    let name = Path::new(path).file_name().map(|n| n.to_string_lossy()).unwrap_or_default();
    out.push(format!(
        "The rules live in .clang-tidy and AGENTS.md. Fix {name} now, before anything else."
    ));
    2
}

fn staged(dir: Option<&Path>, out: &mut Vec<String>) -> Result<u8, String> {
    // No dir: the hook answers for the repository it was invoked in.
    let mut command = Command::new("git");
    if let Some(dir) = dir {
        command.current_dir(dir);
    }
    let diff = command
        .args(["diff", "--cached", "-U0", "-M"])
        .output()
        .map_err(|e| format!("can't run git: {e}"))?;
    let diff = String::from_utf8_lossy(&diff.stdout);
    let hunk = Regex::new(r"\+(\d+)").unwrap();
    let (mut path, mut number) = (String::new(), 0usize);
    let mut added: BTreeMap<String, Vec<(usize, String)>> = BTreeMap::new();
    let mut dropped: HashSet<String> = HashSet::new();
    for line in diff.split('\n') {
        if let Some(rest) = line.strip_prefix("+++ b/") {
            (path, number) = (rest.to_string(), 0);
        } else if line.starts_with("@@") {
            number = hunk.captures(line).and_then(|c| c[1].parse().ok()).unwrap_or(1) - 1;
        } else if line.starts_with('-') && !line.starts_with("---") {
            dropped.insert(comment_text(&line[1..]).to_string());
        } else if line.starts_with('+') && !line.starts_with("+++") {
            number += 1;
            if is_source(&path) {
                added.entry(path.clone()).or_default().push((number, line[1..].to_string()));
            }
        }
    }
    let banned = banned();
    let (mut bad, mut plain, mut marked) = (0, 0, 0);
    for (path, lines) in &added {
        for (name, into) in &banned {
            let calls = calls(name);
            for (number, line) in lines {
                if calls.is_match(&without_comments(line)) {
                    out.push(format!("{path}:{number}: {name} is banned -- write {into} instead."));
                    bad += 1;
                }
            }
        }
        if format!("/{path}").contains(HARNESS) {
            continue;
        }
        for (number, block) in blocks(lines) {
            if block.iter().any(|l| l.contains(LICENSE)) {
                continue;
            }
            if block.iter().all(|l| dropped.contains(comment_text(l))) {
                continue;
            }
            if let Some(why) = fault(&block) {
                out.push(format!("{path}:{number}: {why}."));
                bad += 1;
            } else if block[0].trim().starts_with(MARKER) {
                marked += 1;
            } else {
                plain += block.len();
            }
        }
    }
    if plain > PLAIN_LINES {
        out.push(format!(
            "this commit adds {plain} comment lines; {PLAIN_LINES} is the budget, \
             and one marked block on top of it."
        ));
        bad += 1;
    }
    if marked > MARKED_BLOCKS {
        out.push(format!(
            "this commit takes {marked} {MARKER} exceptions; {MARKED_BLOCKS} is the budget."
        ));
        bad += 1;
    }
    Ok(if bad > 0 { 1 } else { 0 })
}

#[cfg(test)]
mod tests;
