#!/bin/sh
# The house rules check: `komaru rules` from Telegram/rust/komaru_tools.
root=$(cd "$(dirname "$0")/../.." && pwd)
PATH="$HOME/.cargo/bin:$PATH" exec cargo run -q --release --locked \
    --manifest-path "$root/Telegram/rust/komaru_tools/Cargo.toml" -- rules "$@"
