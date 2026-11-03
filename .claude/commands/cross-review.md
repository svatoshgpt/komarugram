---
description: Review uncommitted changes or the last N commits with independent reviewer agents, then arbitrate, fix and re-review until it converges
allowed-tools: Read, Write, Edit, Glob, Grep, Bash, Agent, SendMessage, AskUserQuestion, TodoWrite
---

# Cross Review

Read `.agents/shared/cross-review/protocol.md` and
`.agents/shared/cross-review/prompts.md` completely. You are the runner and
the final arbiter; every reviewer is a fresh `Agent` that sees only its
prompt, the change and the repository — never your reasoning, the ledger or
another reviewer's report.

## Arguments

Read the arguments below as natural language:

- nothing: the uncommitted changes;
- "N commits", "last N commits" or `HEAD~N`: the last N commits;
- "max K rounds": a round cap other than ten;
- "review only" or "no fixes": one round, arbitration and a report, with no
  edits;
- "resume" with a session path, or "resume last": continue an earlier session
  from its ledger and its first incomplete step;
- anything else: extra focus passed to every reviewer as `<FOCUS>`.

## Run

The scope helper is the `komaru` tool from `Telegram/rust/komaru_tools`. Build
it once per session and call it by path:

```bash
cargo build -q --release --manifest-path Telegram/rust/komaru_tools/Cargo.toml
KOMARU=Telegram/rust/komaru_tools/target/release/komaru
```

1. Freeze the scope with `$KOMARU review init --source-root SOURCE_ROOT
   [--commits N]`, run the round-1 `$KOMARU review snapshot --session SESSION
   --round 1`, and tell the human the session directory and the scope's file
   count and size in one line. Exit code 3 (an empty scope) ends the command.
2. For each round, fill the prompts from `prompts.md` into
   `SESSION/roundR/prompts/` and start every reviewer of the round as
   parallel foreground `Agent` calls in one message, each with its prompt
   file's text as the whole prompt. Each reviewer returns its report; save it
   as `SESSION/roundR/<role>.md`.
3. A report without its `## Verdict:` line is rerun once in a fresh agent. If
   a lens fails twice, continue without it and list the gap in the report; if
   the general reviewer fails twice, stop and report.
4. When lens reviewers ran, send the synthesis prompt to the general reviewer
   with `SendMessage` (its agent id from step 2), so it continues its own
   session, and save the reply as `SESSION/roundR/review.md`. Without lens
   reviewers, the focused general report is the round's review.
5. Arbitrate every finding under the protocol. Read the code yourself, check
   the ledger for reversals, and write each decision to `SESSION/ledger.md`
   before editing anything.
6. Apply the FIX decisions unless the arguments asked for review only. Follow
   AGENTS.md and REVIEW.md. When C++, CMake, style or resource files changed,
   build as AGENTS.md describes and prove the build per its rules; fix a
   broken build before the next round. Snapshot the next round, then check
   the comment rules over that snapshot with
   `GIT_INDEX_FILE=SESSION/index python3 tools/rules/hook.py --staged`.
7. Apply the protocol's stop conditions, then start the next round's focused
   reviewers or finish.

Never commit, amend, rebase or stash. When the loop ends, give the protocol's
report in a few short paragraphs and offer to commit the fixes.

Arguments:

```text
$ARGUMENTS
```
