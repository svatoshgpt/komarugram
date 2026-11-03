//! Steps of .github/workflows/win.yml that need more than a shell line.

use std::fs;
use std::path::Path;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use crate::args::Args;
use crate::git;

pub fn run(list: &[String]) -> Result<u8, String> {
    let Some((command, rest)) = list.split_first() else {
        return Err("expected keep-dates, packer-key or dockerfile".into());
    };
    let args = Args::parse(rest)?;
    match command.as_str() {
        "keep-dates" => keep_dates(&args),
        "packer-key" => packer_key(&args),
        "dockerfile" => dockerfile(&args),
        _ => Err(format!("unknown command {command}")),
    }
}

fn set_time(path: &Path, time: SystemTime) {
    if let Ok(file) = fs::OpenOptions::new().write(true).open(path) {
        let _ = file.set_modified(time);
    }
}

fn touch_tree(path: &Path, time: SystemTime) {
    let Ok(entries) = fs::read_dir(path) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            touch_tree(&path, time);
        } else {
            set_time(&path, time);
        }
    }
}

/// Checkout gives every file a new date, so MSBuild would rebuild everything
/// despite the cached objects. Files unchanged since the cached build get an
/// old date and only the changed ones are compiled again.
fn keep_dates(args: &Args) -> Result<u8, String> {
    let repo = Path::new(args.required("repo")?);
    let old = args.required("old")?;
    let head = git::run(repo, &["rev-parse", "HEAD"])?;
    if old.len() != 40 || old == head {
        println!("Cache from {old}, nothing to compare");
        return Ok(0);
    }
    if git::run(repo, &["fetch", "-q", "--depth=1", "origin", old]).is_err() {
        println!("::warning::Commit {old} of the cached build is gone, full rebuild");
        return Ok(0);
    }
    let changed = git::run(repo, &["diff", "--name-only", old, "HEAD"])?;
    let tracked = git::run(repo, &["ls-files", "--recurse-submodules"])?;
    println!("Changed since the cached build: {} paths", changed.lines().count());

    let past = UNIX_EPOCH + Duration::from_secs(1_577_836_800); // 2020-01-01
    let mut dated = 0;
    for path in tracked.lines() {
        set_time(&repo.join(path), past);
        dated += 1;
    }
    let now = SystemTime::now();
    for path in changed.lines() {
        let path = repo.join(path);
        // A submodule path changes as a whole when its commit moves.
        if path.is_dir() {
            touch_tree(&path, now);
        } else if path.exists() {
            set_time(&path, now);
        }
    }
    println!("dated {dated} files");
    Ok(0)
}

/// A C string literal of `text` with a trailing newline.
fn c_literal(text: &str) -> String {
    let mut result = String::from("\"");
    for c in text.chars().chain(std::iter::once('\n')) {
        match c {
            '"' => result.push_str("\\\""),
            '\\' => result.push_str("\\\\"),
            '\n' => result.push_str("\\n"),
            '\r' => result.push_str("\\r"),
            '\t' => result.push_str("\\t"),
            c if (c as u32) < 0x20 => result.push_str(&format!("\\x{:02x}", c as u32)),
            c => result.push(c),
        }
    }
    result.push('"');
    result
}

/// Writes the updater signing key from UPDATE_PRIVATE_KEY into a header.
fn packer_key(args: &Args) -> Result<u8, String> {
    let out = args.required("out")?;
    let key = std::env::var("UPDATE_PRIVATE_KEY")
        .map_err(|_| "UPDATE_PRIVATE_KEY is not set".to_string())?
        .replace("\r\n", "\n");
    let header = format!(
        "// Generated in CI from a secret. Never commit.\n\
         const char *PrivateKey = {};\n\
         const char *PrivateBetaKey = \"\";\n\
         static const char *AlphaPrivateKey = \"\";\n",
        c_literal(key.trim())
    );
    fs::write(out, header).map_err(|e| format!("can't write {out}: {e}"))?;
    Ok(0)
}

/// A switch of the Dockerfile template: set and empty turns it off.
fn switch(name: &str, default: bool) -> bool {
    std::env::var(name).map(|v| !v.is_empty()).unwrap_or(default)
}

fn render(template: &str) -> Result<String, String> {
    let mut env = minijinja::Environment::new();
    env.add_template("Dockerfile", template).map_err(|e| e.to_string())?;
    let context = minijinja::context! {
        DEBUG => switch("DEBUG", true),
        MINSIZE => switch("MINSIZE", false),
        LTO => switch("LTO", true),
        ASAN => switch("ASAN", false),
        JOBS => std::env::var("JOBS").unwrap_or_default(),
    };
    env.get_template("Dockerfile")
        .and_then(|t| t.render(context))
        .map_err(|e| e.to_string())
}

/// Renders Telegram/build/docker/centos_env/Dockerfile to stdout.
fn dockerfile(args: &Args) -> Result<u8, String> {
    let path = args.required("template")?;
    let template = fs::read_to_string(path).map_err(|e| format!("can't read {path}: {e}"))?;
    // Jinja2 reads every line ending as \n, so a CRLF checkout renders the same.
    let template = template.replace("\r\n", "\n");
    println!("{}", render(&template)?);
    Ok(0)
}

#[cfg(test)]
mod tests {
    #[test]
    fn literal() {
        assert_eq!(super::c_literal("a\"b\\c\nd"), "\"a\\\"b\\\\c\\nd\\n\"");
    }

    #[test]
    fn template() {
        let text = "a\n{%- if JOBS %}\nb{% else %}c{% endif %} {{ JOBS or \"x\" }}\n";
        assert_eq!(super::render(text).unwrap(), "ac x");
    }
}
