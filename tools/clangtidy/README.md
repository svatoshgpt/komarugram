# clang-tidy for tdesktop

```bash
cargo run -q --release --manifest-path Telegram/rust/komaru_tools/Cargo.toml -- tidy compile-db   # after each configure
git diff -U0 HEAD | clang-tidy-diff.py -p1 -path out/clang-tidy -j 10 -quiet
run-clang-tidy -p out/clang-tidy -j 10 -quiet <files>
```

Both runners ship with LLVM, under `share/clang`. Needs clang-tidy 20 or
newer: an older one ignores `CustomFunctions` without a word.

The commit hook, once per clone, submodules included; its tests run with
`cargo test --manifest-path Telegram/rust/komaru_tools/Cargo.toml`:

```bash
for repo in . $(git config -f .gitmodules --get-regexp path \
        | awk '{print $2}' | grep -v ThirdParty); do
    hooks=$(git -C "$repo" rev-parse --git-path hooks)/pre-commit
    printf '#!/bin/sh\nexec sh %s/tools/rules/hook.sh --staged\n' \
        "$PWD" > "$hooks" && chmod +x "$hooks"
done
```
