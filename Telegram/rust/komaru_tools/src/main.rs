//! `komaru`: helpers for the development workflow and CI.
//!
//!   komaru review init --source-root ROOT [--commits N] [--session DIR]
//!   komaru review snapshot --session DIR --round R
//!   komaru ci keep-dates --repo DIR --old SHA
//!   komaru ci packer-key --out FILE
//!   komaru rules [--staged]   (an edit hook reading JSON from stdin)
//!   komaru ci dockerfile --template FILE
//!   komaru ci free-disk --path DIR... [--timeout SECONDS]
//!   komaru ci prune-libs --dir LIBRARIES
//!   komaru tidy compile-db [BUILD]
//!   komaru tl-permissions --scheme api.tl --out table.cpp

mod args;
mod ci;
mod git;
mod permissions;
mod review;
mod rules;
mod sweep;
mod tidy;

use std::process::ExitCode;

fn usage() -> ExitCode {
    eprintln!("usage: komaru review <init|snapshot> ...");
    eprintln!("       komaru ci <keep-dates|packer-key|dockerfile|free-disk|prune-libs> ...");
    eprintln!("       komaru rules [--staged]");
    eprintln!("       komaru tidy compile-db [BUILD]");
    eprintln!("       komaru tl-permissions --scheme FILE --out FILE");
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
        "rules" => rules::run(rest),
        "tidy" => tidy::run(rest),
        "tl-permissions" => permissions::run(rest),
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
