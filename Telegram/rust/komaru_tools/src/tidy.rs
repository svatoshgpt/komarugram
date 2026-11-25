//! Writes the database clang-tidy reads: `komaru tidy compile-db [build dir]`.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;

use serde_json::{json, Value};

const GCC_ONLY: &[&str] = &["-fhardened"];
const MSVC_NOISE: &[&str] = &["MP", "WX", "FS", "showIncludes"];
const MSVC_FILES: &[&str] = &["Yu", "Yc", "Fp", "Fo", "Fd"];

pub fn run(list: &[String]) -> Result<u8, String> {
    match list.split_first() {
        Some((command, rest)) if command == "compile-db" => {
            compile_db(rest.first().map(String::as_str).unwrap_or("out"))
        }
        _ => Err("expected compile-db".into()),
    }
}

fn file_name(path: &str) -> String {
    Path::new(path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
}

/// The program `name` as found on PATH, like `shutil.which`.
fn which(name: &str, path: Option<&str>) -> Option<PathBuf> {
    let path = path.map(String::from).or_else(|| std::env::var("PATH").ok())?;
    let extensions: Vec<String> = if cfg!(windows) {
        let list = std::env::var("PATHEXT").unwrap_or(".EXE;.BAT;.CMD".into());
        std::iter::once(String::new()).chain(list.split(';').map(str::to_lowercase)).collect()
    } else {
        vec![String::new()]
    };
    std::env::split_paths(&path).find_map(|dir| {
        extensions.iter().map(|ext| dir.join(format!("{name}{ext}"))).find(|p| p.is_file())
    })
}

/// Where clang-tidy sits, since its clang++ has to stand in for ours.
fn tools_directory() -> Result<PathBuf, String> {
    let tidy = std::env::var("CLANG_TIDY")
        .ok()
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
        .or_else(|| which("clang-tidy", None))
        .or_else(|| which("clang-tidy", Some("/opt/homebrew/opt/llvm/bin")))
        .ok_or("clang-tidy not found; set CLANG_TIDY")?;
    Ok(tidy.parent().map(Path::to_path_buf).unwrap_or_default())
}

/// clang-cl stands in for MSVC, clang++ for every other compiler.
fn compiler_for(original: &str, directory: &Path) -> String {
    let msvc = matches!(file_name(original).to_lowercase().as_str(), "cl" | "cl.exe");
    let name = format!(
        "{}{}",
        if msvc { "clang-cl" } else { "clang++" },
        if cfg!(windows) { ".exe" } else { "" }
    );
    let beside = directory.join(&name);
    if beside.exists() { beside.to_string_lossy().into_owned() } else { name }
}

/// POSIX shell words, as `shlex.split` reads them.
fn split_posix(command: &str) -> Vec<String> {
    let (mut out, mut word, mut any) = (vec![], String::new(), false);
    let mut chars = command.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            c if c.is_whitespace() => {
                if any {
                    out.push(std::mem::take(&mut word));
                    any = false;
                }
            }
            '\\' => {
                any = true;
                if let Some(next) = chars.next() {
                    word.push(next);
                }
            }
            '\'' => {
                any = true;
                for c in chars.by_ref() {
                    if c == '\'' {
                        break;
                    }
                    word.push(c);
                }
            }
            '"' => {
                any = true;
                while let Some(c) = chars.next() {
                    match c {
                        '"' => break,
                        '\\' if matches!(chars.peek(), Some('\\' | '"' | '$' | '`' | '\n')) => {
                            word.push(chars.next().unwrap());
                        }
                        c => word.push(c),
                    }
                }
            }
            c => {
                any = true;
                word.push(c);
            }
        }
    }
    if any {
        out.push(word);
    }
    out
}

/// Windows words by the CommandLineToArgvW rules, which shlex cannot read.
fn split_windows(command: &str) -> Vec<String> {
    let chars: Vec<char> = command.chars().collect();
    let mut out = vec![];
    let mut i = 0;
    // The program name: quotes group, backslashes are plain.
    let mut word = String::new();
    let mut quoted = false;
    while i < chars.len() {
        let c = chars[i];
        i += 1;
        if c == '"' {
            quoted = !quoted;
        } else if !quoted && (c == ' ' || c == '\t') {
            break;
        } else {
            word.push(c);
        }
    }
    out.push(word);
    loop {
        while i < chars.len() && (chars[i] == ' ' || chars[i] == '\t') {
            i += 1;
        }
        if i >= chars.len() {
            break;
        }
        let (mut word, mut quoted) = (String::new(), false);
        while i < chars.len() {
            let c = chars[i];
            if c == '\\' {
                let start = i;
                while i < chars.len() && chars[i] == '\\' {
                    i += 1;
                }
                let count = i - start;
                if i < chars.len() && chars[i] == '"' {
                    word.extend(std::iter::repeat('\\').take(count / 2));
                    if count % 2 == 1 {
                        word.push('"');
                        i += 1;
                    }
                } else {
                    word.extend(std::iter::repeat('\\').take(count));
                }
                continue;
            }
            if c == '"' {
                if quoted && chars.get(i + 1) == Some(&'"') {
                    word.push('"');
                    i += 2;
                    continue;
                }
                quoted = !quoted;
                i += 1;
                continue;
            }
            if !quoted && (c == ' ' || c == '\t') {
                break;
            }
            word.push(c);
            i += 1;
        }
        out.push(word);
    }
    out
}

fn split_command(command: &str) -> Vec<String> {
    if command.trim().is_empty() {
        return vec![];
    }
    if cfg!(windows) { split_windows(command) } else { split_posix(command) }
}

/// An option clang rejects or cannot use; /FI stays, it force-includes.
fn dropped(argument: &str) -> bool {
    if GCC_ONLY.contains(&argument) {
        return true;
    }
    if !argument.starts_with(['/', '-']) {
        return false;
    }
    let flag = &argument[1..];
    MSVC_FILES.iter().any(|p| flag.starts_with(p)) || MSVC_NOISE.contains(&flag)
}

/// What clang-tidy's clang needs and the build's own compiler did not.
fn platform_arguments(driver: &str) -> Vec<String> {
    if cfg!(target_os = "macos") {
        let sdk = Command::new("xcrun")
            .arg("--show-sdk-path")
            .output()
            .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
            .unwrap_or_default();
        if sdk.is_empty() {
            return vec![];
        }
        // Its own libc++ is newer than ours and rejects some of our files.
        return vec![
            "-isysroot".into(),
            sdk.clone(),
            "-nostdinc++".into(),
            format!("-isystem{sdk}/usr/include/c++/v1"),
        ];
    }
    if cfg!(windows) {
        // MSVC backs an enum of no fixed type by int, so mtpc_ cases narrow.
        return vec!["-Wno-c++11-narrowing".into()];
    }
    let name = file_name(driver);
    if !name.starts_with("gcc") && !name.starts_with("g++") {
        return vec![];
    }
    let found = which(driver, None).unwrap_or_else(|| PathBuf::from(driver));
    let prefix = found.parent().and_then(Path::parent).map(Path::to_path_buf);
    match prefix.filter(|p| p.is_dir()) {
        Some(prefix) => vec![format!("--gcc-toolchain={}", prefix.display())],
        None => {
            eprintln!(
                "warning: {driver} not on PATH, so the standard library it carries stays \
                 hidden from clang"
            );
            vec![]
        }
    }
}

/// The command without what clang-tidy's clang cannot take.
fn rewrite(command: &str) -> Vec<String> {
    let arguments = split_command(command);
    let (mut kept, mut index) = (vec![], 0);
    while index < arguments.len() {
        let argument = &arguments[index];
        // Only clang's own form: GCC's -include of the header has to stay.
        if argument == "-Xclang" && arguments.get(index + 1).is_some_and(|a| a == "-include-pch") {
            index += 4;
            continue;
        }
        if index > 0 && dropped(argument) {
            index += 1;
            continue;
        }
        kept.push(argument.clone());
        index += 1;
    }
    if kept.first().is_some_and(|k| matches!(file_name(k).as_str(), "ccache" | "sccache")) {
        kept.remove(0);
    }
    kept
}

fn compile_db(build: &str) -> Result<u8, String> {
    if !Path::new(build).join("build.ninja").exists() {
        return Err(format!("no ninja build in {build}"));
    }
    let raw = Command::new("ninja")
        .args(["-C", build, "-t", "compdb"])
        .output()
        .map_err(|e| format!("can't run ninja: {e}"))?;
    if !raw.status.success() {
        return Err("ninja -t compdb failed".into());
    }
    let list: Vec<Value> = serde_json::from_slice(&raw.stdout).map_err(|e| e.to_string())?;
    let mut commands = vec![];
    for entry in &list {
        let file = entry["file"].as_str().unwrap_or("");
        let command = entry["command"].as_str().unwrap_or("");
        if ![".cpp", ".mm", ".cxx"].iter().any(|e| file.ends_with(e)) {
            continue;
        }
        if command.trim().is_empty() || !Path::new(file).exists() {
            continue;
        }
        let kept = rewrite(command);
        if !kept.is_empty() {
            commands.push((entry, kept));
        }
    }
    let directory = tools_directory()?;
    let mut drivers: HashMap<&str, usize> = HashMap::new();
    for (_, kept) in &commands {
        *drivers.entry(&kept[0]).or_default() += 1;
    }
    let driver = drivers.iter().max_by_key(|(_, n)| **n).map(|(d, _)| *d).unwrap_or("");
    let extra = platform_arguments(driver);
    let entries: Vec<Value> = commands
        .iter()
        .map(|(entry, kept)| {
            let mut arguments = vec![compiler_for(&kept[0], &directory)];
            arguments.extend(extra.iter().cloned());
            arguments.extend(kept[1..].iter().cloned());
            json!({
                "directory": entry["directory"],
                "file": entry["file"],
                "arguments": arguments,
            })
        })
        .collect();
    let target = Path::new(build).join("clang-tidy");
    std::fs::create_dir_all(&target).map_err(|e| e.to_string())?;
    let text = serde_json::to_string_pretty(&entries).map_err(|e| e.to_string())?;
    std::fs::write(target.join("compile_commands.json"), text).map_err(|e| e.to_string())?;
    println!("{} entries -> {}/compile_commands.json", entries.len(), target.display());
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn posix_words() {
        assert_eq!(split_posix(r#"g++ -DA="b c" 'x y' z\ w"#), ["g++", "-DA=b c", "x y", "z w"]);
    }

    #[test]
    fn windows_words() {
        assert_eq!(
            split_windows(r#""C:\Program Files\cl.exe" /DA=\"b\" "c d" e\\f"#),
            [r"C:\Program Files\cl.exe", r#"/DA="b""#, "c d", r"e\\f"]
        );
    }

    #[test]
    fn rewrites() {
        let kept = rewrite("ccache g++ -fhardened -Xclang -include-pch -Xclang a.pch -c a.cpp");
        assert_eq!(kept, ["g++", "-c", "a.cpp"]);
        assert!(dropped("/Yuabc") && dropped("-MP") && !dropped("/FIa.h"));
    }
}
