# Branch and worktree audit, 7 November 2026

Read-only: nothing was deleted, moved or merged. Compared with `master` at `d1b2de9`. "In master" counts the meaningful added lines of a worktree's uncommitted edits (lines over 12 characters, ignoring line endings) that already appear in the same file on `master`. A recommendation is not an action: the owner decides every deletion.

## Branches

| Branch | State | Recommendation |
|---|---|---|
| `p3-gate` (28 commits, last 2 November) | Every line of its two unique changes (`95df350`, `f7b8327`) is already in `master` | Safe to delete the branch |
| `codex/setup-readme-proposal` (3 commits) | Its README wording is merged and corrected on branch `release-prep-readme` (`3f80497`) | Keep until the release decision, then delete both |
| `release-prep-readme` (new, pushed) | The corrected release README; **not merged to `master`** (its Releases link is dead before a release) | Merge when the owner decides to publish |

## Worktrees with uncommitted edits that are mine or the owner's

| Worktree | Base | Edits | Already in master | What the rest is | Recommendation |
|---|---|---|---|---|---|
| `C:/Users/Tideg/f117-recomp-local/clip04dd` (`p2-clip-04dd`) | 31 Oct, 334 commits behind | 28 files, 1,748 added lines | 95% | The old copy of host/presentation work that later landed in `master` | Superseded; delete the worktree (the branch has no commits of its own) |
| `C:/Users/Tideg/f117-recomp-local/wt` (detached) | 10 Oct, 872 commits behind | 24 files, 1,640 lines | 89% | Early drawlist and hires work, long since reworked | Superseded; delete |
| `D:/f117-wt/gate-h` (`airair-profile-20261017`) | 17 Oct, 647 behind | 2 files, 73 lines | 8% | A profiling hack: a hard-coded hidden 4K window and a per-stage log file | Obsolete: `--present-log`, `F117_LOOP_TIMING`, `F117_SLOW_FRAME_TRACE` and `F117_WORKER_TIMING` cover it; delete |
| `D:/f117-wt/credit` (`credit-bytes`) | 16 Oct, 702 behind | 4 files, 128 lines | 62% | Byte-level census credit (`named_bytes`, `F117_NAMED_COVER`), which is in `master` in its final form; the rest is older wording | Superseded; delete |

## Others

| Item | Finding | Recommendation |
|---|---|---|
| Worktree at `C:/Users/Tideg/My Drive/Development/F-117A Recomp/.claude/worktrees/agent-ada5a0330bc7d4569` | Its folder no longer exists (the repository moved off Google Drive on 26 October) | `git worktree prune` (removes only git's record) |
| Two `.claude/worktrees/agent-*` worktrees in the main checkout (29 October) | One-file `matched.c` edits (2 to 3 lines) | Phase 2's: ask the P2 session; probably deletable |
| Nine worktrees with `matched.c` / `func_lockstep.c` edits (`D:/f117-wt/*`, `D:/f117-gate`, `p2-completion-20261101`), 13 Oct to 1 Nov | Phase 2 work in progress or abandoned | Not touched: the P2 session decides |
| `C:/Dev/f117-bench` (590 MB, 5 November) | Frame traces and scratch scripts; no tool uses this path (`bench.sh` and `guards.sh` use `C:\Users\Tideg\f117-bench`, 182 MB, which is in use) | The owner may delete it; the traces can be regenerated |
| `tools/sound_board_*` edits in the main checkout | The owner's, uncommitted | The owner's call |
