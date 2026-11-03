use std::path::Path;
use std::process::Command;

/// Runs git in `root`, returning stdout bytes or the error text.
pub fn run_bytes(root: &Path, args: &[&str], index: Option<&Path>) -> Result<Vec<u8>, String> {
    let mut command = Command::new("git");
    command.arg("-C").arg(root).args(args);
    if let Some(index) = index {
        command.env("GIT_INDEX_FILE", index);
    }
    let output = command.output().map_err(|e| format!("can't run git: {e}"))?;
    if !output.status.success() {
        return Err(format!(
            "git {} failed: {}",
            args.join(" "),
            String::from_utf8_lossy(&output.stderr).trim()
        ));
    }
    Ok(output.stdout)
}

pub fn run(root: &Path, args: &[&str]) -> Result<String, String> {
    run_in(root, args, None)
}

pub fn run_in(root: &Path, args: &[&str], index: Option<&Path>) -> Result<String, String> {
    Ok(String::from_utf8_lossy(&run_bytes(root, args, index)?).trim().to_string())
}
