//! `komaru`: helpers for the development workflow and CI.
//!
//!   komaru review init --source-root ROOT [--commits N] [--session DIR]
//!   komaru review snapshot --session DIR --round R
//!   komaru ci keep-dates --repo DIR --old SHA
//!   komaru ci packer-key --out FILE

mod args;
mod ci;
mod git;
mod review;

use std::process::ExitCode;

fn usage() -> ExitCode {
    eprintln!("usage: komaru review <init|snapshot> ...");
    eprintln!("       komaru ci <keep-dates|packer-key> ...");
    ExitCode::from(2)
}

fn main() -> ExitCode {
    let all: Vec<String> = std::env::args().skip(1).collect();
    let Some((group, rest)) = all.split_first() else {
        return usage();
    };
    let result = match group.as_str() {
        "review" => review::run(rest),
        "ci" => ci::run(rest),
        _ => return usage(),
    };
    match result {
        Ok(code) => ExitCode::from(code),
        Err(error) => {
            eprintln!("komaru {group}: {error}");
            ExitCode::from(1)
        }
    }
}
