# Progress tracking

Every commit title starts with the current progress (it ended with it until
7 Oct 2026; older commits keep the suffix), for example
`(P1 100.00%, P2 67.82%, P3 96.33%, All 89.25%) Subject`, printed by
`py tools/progress.py --title`. `py tools/progress.py` alone prints every item
with its weight, completion and kind. This page says what the numbers mean, so
that they can be checked and argued with. What each item covers is in
[roadmap.md](roadmap.md).

## The method

`docs/progress.json` lists each phase's work as items. Each item has:

- **weight**: its share of that phase's total effort, done and remaining, as
  a judgement of work (not of importance). Weights within a phase sum to 100.
- **done**: its completion from 0 to 1.
- **kind**: `measured` when `tools/progress.py` computes `done` from the
  repository, `estimate` when it is a judgement. Every estimate carries its
  evidence beside it, and an estimate is only raised with new evidence.

A phase's percentage is its items' weighted completion. **All** is the
scored phases weighted by their share of the effort: Phase 1 42, Phase 2 29,
Phase 3 24 (normalised over their sum). Phase 1 is the largest because building
an exact recompilation, its machine and its proof was the bulk of the work;
Phases 2 and 3 are large too (thousands of routines to name and match; a new
presentation path for 60+ fps and 4K), and their weights say so.

Items closed by scope keep only the weight of the work done (8 Oct 2026: the
roadmap's "Settled" list), so dropping work never reads as finishing it.
Phase 4 (what only a person or an independent reference can settle: a person
playing the game, Roland output against a reference with a listening check) is
not scored (owner, 15 Oct 2026: its estimates had stopped meaning anything); it
stays on the roadmap.

## What is measured

- **Phase 2, matching and naming:** the census (the Reimp's walk of seven
  programs: 1,535 functions, 179,213 bytes, recorded in progress.json) against
  the matched routines in `src/matched/matched.c`, by code bytes, so a
  three-instruction helper does not count like the flight model. A routine
  counts as named only when it has a named, explained matched equivalent in
  the repository; name leads from `tools/reimp_names.py` do not count until
  they are checked and used. **Credit is by byte** (owner, 16 October 2026):
  a census byte counts once it lies inside an original instruction that a
  matched body runs at its own level, as measured by the lockstep. So a body
  that names only a census entry's RET earns that RET, a body that runs a
  shared tail or another entry's code inline earns those bytes once, and a
  row holding library code the game never runs (printf's `%n`, `%p` and
  floating-point arms) is credited only for the arm it uses. Fixture states
  count alongside routes, as for the proofs themselves. Refresh after every
  batch: run `func_lockstep` with `F117_NAMED_COVER=FILE` (one file per shard
  in a directory; it appends one line per matched routine), then
  `py tools/progress.py --census <reimp_names.py output> --named-cover DIR
  --data <game directory>`. The refresh stops if any matched routine has no
  coverage line.
- **Phase 3, fixes:** fixes available in `src/fixes/fixes.c` over the
  actionable reports catalogued as `### Dn.` in `docs/bugs.md`. An explicit
  `Disposition. Closed:` excludes an unestablished historical report;
  `Fixed by:` credits a report addressed by another available fix (D7 by D8).
  Closed reports are printed separately and never counted as fixes. Closing
  D10, D35 and D36 (13 Oct 2026) cut this item's weight from 35 to 27.5
  (11/14 of 35) before Phase 3's weights were renormalised; its 11 of 11 does
  not mean three new fixes shipped.

## What is estimated, and how to be honest about it

- Phase 1's completed work (translation, lockstep, routes, machine fidelity)
  is one item at 100%, backed by the parity evidence; each other item is an
  estimate with its gap named.
- An estimate is not raised for effort spent, only for something now shown
  (a gate that passes, a capture that matches, a defect found and fixed).
- When something thought done turns out not to be, its estimate goes down in
  the same commit that records the finding.
- Weights are revisited when the remaining work is understood better; a
  weight change is its own commit, so the percentages' jump is visible. The
  largest so far: on 15 Oct 2026 Phase 3's "interpolation to 60+ fps" (0.85)
  was split into Stage 4 infrastructure (0.9) and Stage 3, smooth in-between
  frames (0.15), because the owner judged the motion not acceptable; P3 fell
  from 95.79% to 82.75%. After the owner played `--present rerun` fullscreen at
  4K ("good at least for now", 15 Oct, 22:32) Stage 3 went to 0.9: P3 96.33%.
