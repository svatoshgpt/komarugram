//! Steps of .github/workflows/win.yml that need more than a shell line.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crate::args::Args;
use crate::git;
use crate::sweep;

pub fn run(list: &[String]) -> Result<u8, String> {
    let Some((command, rest)) = list.split_first() else {
        return Err("expected keep-dates, packer-key, dockerfile, free-disk or prune-libs".into());
    };
    let args = Args::parse(rest)?;
    match command.as_str() {
        "keep-dates" => keep_dates(&args),
        "packer-key" => packer_key(&args),
        "dockerfile" => dockerfile(&args),
        "free-disk" => free_disk(&args),
        "prune-libs" => prune_libs(&args),
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
    for relative in changed.lines() {
        let path = repo.join(relative);
        if !path.is_dir() {
            if path.exists() {
                set_time(&path, now);
            }
            continue;
        }
        // A moved submodule gets only its own changed files touched, not the whole tree.
        match submodule_changes(repo, old, relative) {
            Some(files) => {
                println!("{relative}: {} files changed", files.len());
                for file in files {
                    let file = path.join(file);
                    if file.is_dir() {
                        touch_tree(&file, now);
                    } else if file.exists() {
                        set_time(&file, now);
                    }
                }
            }
            None => touch_tree(&path, now),
        }
    }
    println!("dated {dated} files");
    Ok(0)
}

/// Files that differ in the submodule at `path` between its commit in `old` and its checkout.
fn submodule_changes(repo: &Path, old: &str, path: &str) -> Option<Vec<String>> {
    let entry = git::run(repo, &["ls-tree", old, "--", path]).ok()?;
    let mut fields = entry.split_whitespace();
    if fields.next()? != "160000" {
        return None;
    }
    let commit = fields.nth(1)?;
    let submodule = repo.join(path);
    let object = format!("{commit}^{{commit}}");
    if git::run(&submodule, &["cat-file", "-e", &object]).is_err() {
        git::run(&submodule, &["fetch", "-q", "--depth=1", "origin", commit]).ok()?;
    }
    let files = git::run(&submodule, &["diff", "--name-only", commit, "HEAD"]).ok()?;
    Some(files.lines().map(str::to_string).collect())
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

fn gigabytes(bytes: Option<u64>) -> String {
    bytes.map_or("?".into(), |b| format!("{:.1}", b as f64 / (1u64 << 30) as f64))
}

/// Deletes the preinstalled tools the build never uses, every file of them
/// at once, so the biggest folder no longer decides how long it takes.
/// `--timeout` stops it only once `--min-free` GB are free.
fn free_disk(args: &Args) -> Result<u8, String> {
    let roots: Vec<PathBuf> =
        args.all("path").iter().map(PathBuf::from).filter(|p| p.exists()).collect();
    let Some(first) = roots.first() else {
        println!("Nothing to remove");
        return Ok(0);
    };
    let volume: PathBuf = first.components().take(2).collect();
    let before = sweep::free_space(&volume);
    let deadline = args.number("timeout")?.map(|s| Instant::now() + Duration::from_secs(s as u64));
    let wanted = args.number("min-free")?.map(|gb| gb.max(0) as u64 * (1u64 << 30));
    let checked = Mutex::new((Instant::now(), false));
    let stop = || {
        if !deadline.is_some_and(|d| Instant::now() > d) {
            return false;
        }
        let Some(wanted) = wanted else {
            return true;
        };
        let mut state = checked.lock().unwrap();
        let (last, enough) = &mut *state;
        if !*enough && last.elapsed() >= Duration::from_secs(1) {
            *last = Instant::now();
            *enough = sweep::free_space(&volume).is_none_or(|free| free >= wanted);
        }
        *enough
    };
    let started = Instant::now();
    let outcome = sweep::sweep(&roots, &|_: &Path, _: &fs::Metadata| true, false, Some(&stop));
    let names: Vec<String> = roots.iter().map(|r| r.display().to_string()).collect();
    println!("Removed: {}", names.join(", "));
    println!(
        "{} files in {:.0} s, {} could not go{}",
        outcome.files,
        started.elapsed().as_secs_f64(),
        outcome.failed,
        if outcome.stopped { ", stopped at the timeout" } else { "" }
    );
    let after = sweep::free_space(&volume);
    println!("{} free: {} GB -> {} GB", volume.display(), gigabytes(before), gigabytes(after));
    if let (Some(wanted), Some(after)) = (wanted, after) {
        if after < wanted {
            println!(
                "::warning::Only {} GB free on {}, less than the {} GB --min-free asks for: \
                 a build that has to compile Qt again may run out of room",
                gigabytes(Some(after)),
                volume.display(),
                gigabytes(Some(wanted))
            );
        }
    }
    Ok(0)
}

/// What the libraries cache keeps: what the client links and includes, the
/// stage keys, and the source-only stages that later stages build from, such
/// as the Rust crates tdesktop_rust compiles or the headers ffmpeg installs.
fn kept_library(relative: &str, size: u64) -> bool {
    const KEPT_TREES: &[&str] = &[
        "tlottie/",
        "wallet-engine/",
        "nv-codec-headers/",
        "regex/",
        "qt6_windows7/",
    ];
    const KEPT_PARTS: &[&str] =
        &["/include/", "/src/wallet_engine/", "/objects-", "/cache_keys/", "/patches/"];
    const KEPT_EXTENSIONS: &[&str] = &["lib", "a", "exe", "h", "hpp", "inc", "cmake", "pc"];
    if size == 0 {
        return false;
    }
    let path = format!("/{relative}");
    let name = relative.rsplit('/').next().unwrap_or(relative);
    let extension = name.rsplit_once('.').map(|(_, e)| e);
    (KEPT_TREES.iter().any(|t| relative.starts_with(t)) && !path.contains("/target/"))
        || KEPT_PARTS.iter().any(|p| path.contains(p))
        || name == "Makefile"
        || extension.is_some_and(|e| KEPT_EXTENSIONS.contains(&e))
}

/// Shrinks the libraries folder before it is cached.
fn prune_libs(args: &Args) -> Result<u8, String> {
    let root = PathBuf::from(args.required("dir")?);
    let doomed = |path: &Path, meta: &fs::Metadata| {
        if meta.file_type().is_symlink() {
            return false;
        }
        let relative = path.strip_prefix(&root).unwrap_or(path);
        let relative = relative.to_string_lossy().replace('\\', "/");
        !kept_library(&relative, meta.len())
    };
    let started = Instant::now();
    let outcome = sweep::sweep(&[root.clone()], &doomed, true, None);
    println!(
        "Pruned {} files in {:.0} s, {} could not go",
        outcome.files,
        started.elapsed().as_secs_f64(),
        outcome.failed
    );
    Ok(0)
}

#[cfg(test)]
mod tests {
    #[test]
    fn kept_libraries() {
        use super::kept_library as kept;
        assert!(kept("qt_6/out/lib/Qt6Core.lib", 1));
        assert!(kept("openssl/include/openssl/ssl.h", 1));
        assert!(kept("tlottie/src/lib.rs", 1));
        assert!(kept("wallet-engine/Cargo.toml", 1));
        assert!(kept("nv-codec-headers/ffnvcodec.pc.in", 1));
        assert!(kept("cache_keys/qt_6", 1));
        assert!(kept("ffmpeg/Makefile", 1));
        assert!(!kept("qt_6/src/corelib/global.cpp", 1));
        assert!(!kept("tdesktop_rust/target/release/deps/x.rlib", 1));
        assert!(!kept("wallet-engine/bindgen/cpp/bindgen/target/release/x.rlib", 1));
        assert!(!kept("openssl/include/empty.h", 0));
    }

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
