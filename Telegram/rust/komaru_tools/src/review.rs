//! Freezes the scope of a cross-review session and snapshots each round.
//! The contract is .agents/shared/cross-review/protocol.md.
//!
//! `init` fixes the base: HEAD for uncommitted changes, HEAD~N for the last N
//! commits. `snapshot` records the working tree, untracked files included, as
//! a git tree through a private index, so the real index is never touched,
//! and writes the round's scope diff and, after the first round, the diff of
//! the fixes made since the previous snapshot.

use std::fs;
use std::path::{Path, PathBuf};

use serde_json::{json, Value};

use crate::args::Args;
use crate::git;

pub fn run(list: &[String]) -> Result<u8, String> {
    let Some((command, rest)) = list.split_first() else {
        return Err("expected init or snapshot".into());
    };
    let args = Args::parse(rest)?;
    match command.as_str() {
        "init" => init(&args),
        "snapshot" => snapshot(&args),
        _ => Err(format!("unknown command {command}")),
    }
}

fn absolute(path: &str) -> Result<PathBuf, String> {
    std::path::absolute(path).map_err(|e| format!("bad path {path}: {e}"))
}

/// UTC "YYYYmmdd-HHMMSS" for folder names and "YYYY-mm-dd HH:MM:SS".
pub fn utc_now() -> (String, String) {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.as_secs() as i64);
    let (y, m, d, hh, mm, ss) = civil(secs);
    (
        format!("{y:04}{m:02}{d:02}-{hh:02}{mm:02}{ss:02}"),
        format!("{y:04}-{m:02}-{d:02} {hh:02}:{mm:02}:{ss:02}"),
    )
}

/// Calendar date and time of a unix timestamp (Howard Hinnant's algorithm).
pub fn civil(secs: i64) -> (i64, i64, i64, i64, i64, i64) {
    let days = secs.div_euclid(86_400);
    let rest = secs.rem_euclid(86_400);
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + i64::from(m <= 2);
    (y, m, d, rest / 3600, rest % 3600 / 60, rest % 60)
}

fn session_file(session: &Path) -> PathBuf {
    session.join("session.json")
}

fn init(args: &Args) -> Result<u8, String> {
    let root = absolute(args.required("source-root")?)?;
    let commits = args.number("commits")?.unwrap_or(0);
    let top = git::run(&root, &["rev-parse", "--show-toplevel"])?;
    let mut common = PathBuf::from(git::run(&root, &["rev-parse", "--git-common-dir"])?);
    if !common.is_absolute() {
        common = root.join(common);
    }
    let base_ref = if commits > 0 { format!("HEAD~{commits}") } else { "HEAD".into() };
    if commits > 0 {
        let count: i64 = git::run(&root, &["rev-list", "--count", "HEAD"])?.parse().unwrap_or(0);
        if commits >= count {
            return Err(format!("HEAD has only {count} commits"));
        }
    }
    let base = git::run(&root, &["rev-parse", "--verify", &format!("{base_ref}^{{commit}}")])?;
    let (stamp, created) = utc_now();
    let session = match args.get("session") {
        Some(path) => absolute(path)?,
        None => common.join("cross-review").join(stamp),
    };
    if session_file(&session).exists() {
        return Err(format!("session already exists: {}", session.display()));
    }
    fs::create_dir_all(&session).map_err(|e| e.to_string())?;
    let commit_list: Vec<String> = if commits > 0 {
        git::run(&root, &["log", "--format=%h %s", &format!("{base}..HEAD")])?
            .lines()
            .map(String::from)
            .collect()
    } else {
        vec![]
    };
    let data = json!({
        "source_root": absolute(&top)?.display().to_string(),
        "mode": if commits > 0 { "commits" } else { "uncommitted" },
        "commits": commits,
        "base": base,
        "base_subject": git::run(&root, &["log", "-1", "--format=%h %s", &base])?,
        "head_at_start": git::run(&root, &["rev-parse", "HEAD"])?,
        "commit_list": commit_list,
        "dirty_at_start": !git::run(&root, &["status", "--porcelain"])?.is_empty(),
        "created": created,
    });
    fs::write(session_file(&session), serde_json::to_string_pretty(&data).unwrap())
        .map_err(|e| e.to_string())?;
    let mut out = data.as_object().unwrap().clone();
    out.insert("session".into(), json!(session.display().to_string()));
    println!("{}", Value::Object(out));
    Ok(0)
}

fn previous_tree(session: &Path, round: i64) -> Option<(i64, String)> {
    (1..round).rev().find_map(|earlier| {
        fs::read_to_string(session.join(format!("round{earlier}")).join("tree.txt"))
            .ok()
            .map(|tree| (earlier, tree.trim().to_string()))
    })
}

fn snapshot(args: &Args) -> Result<u8, String> {
    let session = absolute(args.required("session")?)?;
    let round = args.number("round")?.ok_or("--round is required")?;
    let data: Value = fs::read_to_string(session_file(&session))
        .ok()
        .and_then(|text| serde_json::from_str(&text).ok())
        .ok_or(format!("no session at {}", session.display()))?;
    let root = PathBuf::from(data["source_root"].as_str().unwrap_or_default());
    let base = data["base"].as_str().unwrap_or_default().to_string();

    let index = session.join("index");
    if !index.exists() {
        let mut real = PathBuf::from(git::run(&root, &["rev-parse", "--git-path", "index"])?);
        if !real.is_absolute() {
            real = root.join(real);
        }
        if real.exists() {
            fs::copy(&real, &index).map_err(|e| e.to_string())?;
        }
    }
    git::run_in(&root, &["add", "-A"], Some(&index))?;
    let tree = git::run_in(&root, &["write-tree"], Some(&index))?;

    let folder = session.join(format!("round{round}"));
    fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
    let write = |name: &str, bytes: &[u8]| {
        fs::write(folder.join(name), bytes).map_err(|e| e.to_string())
    };
    write("tree.txt", format!("{tree}\n").as_bytes())?;
    write("scope.patch", &git::run_bytes(&root, &["diff", "--no-color", "-M", &base, &tree], None)?)?;
    let files = git::run(&root, &["diff", "--name-status", "-M", &base, &tree])?;
    write("files.txt", if files.is_empty() { String::new() } else { format!("{files}\n") }.as_bytes())?;
    let count = files.lines().filter(|l| !l.is_empty()).count();

    let mut summary = json!({
        "round": round,
        "tree": tree,
        "scope_patch": folder.join("scope.patch").display().to_string(),
        "files_txt": folder.join("files.txt").display().to_string(),
        "files": count,
        "scope_stat": git::run(&root, &["diff", "--shortstat", &base, &tree])?,
    });
    if let Some((earlier, earlier_tree)) = previous_tree(&session, round) {
        write(
            "fix.patch",
            &git::run_bytes(&root, &["diff", "--no-color", "-M", &earlier_tree, &tree], None)?,
        )?;
        summary["fix_since_round"] = json!(earlier);
        summary["fix_patch"] = json!(folder.join("fix.patch").display().to_string());
        summary["fix_stat"] = json!(git::run(&root, &["diff", "--shortstat", &earlier_tree, &tree])?);
    }
    println!("{summary}");
    Ok(if count > 0 { 0 } else { 3 })
}
