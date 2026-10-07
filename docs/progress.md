# Progress tracking

Every commit title starts with the current progress (it ended with it until
7 Oct 2026; older commits keep the suffix), for example
`(P1 93.55%, P2 10.76%, P3 39.60%, P4 42.90%, All 52.19%) Subject`, printed by
`py tools/progress.py --title`. This page says what the numbers mean, so
that they can be checked and argued with.

## The method

`docs/progress.json` lists each phase's work as items. Each item has:

- **weight**: its share of that phase's total effort, done and remaining, as
  a judgement of work (not of importance). Weights within a phase sum to 100.
- **done**: its completion from 0 to 1.
- **kind**: `measured` when `tools/progress.py` computes `done` from the
  repository, `estimate` when it is a judgement. Every estimate carries its
  evidence beside it, and an estimate is only raised with new evidence.

A phase's percentage is its items' weighted completion. **All** is the
phases weighted by their share of the whole project's effort: Phase 1 39%,
Phase 2 30%, Phase 3 25%, Phase 4 6%. Phase 1 is the largest because building
an exact recompilation, its machine and its proof was the bulk of the work;
Phases 2 and 3 are large too (thousands of routines to name and match; a new
presentation path for 60+ fps and 4K), and their weights say so. Phase 4 holds
what only a person or an independent reference can settle (a person playing
the game; Roland output against a reference, with a listening check), so the
automated phases can reach 100% without pretending to have settled it.

## What is measured

- **Phase 2, matching and naming:** the census (the Reimp's walk of seven
  programs: 1,535 functions, 179,213 bytes, recorded in progress.json) against
  the matched routines in `src/matched/matched.c`, by code bytes, so a
  three-instruction helper does not count like the flight model. A routine
  counts as named only when it has a named, explained matched equivalent in
  the repository; name leads from `tools/reimp_names.py` do not count until
  they are checked and used. Refresh the census figures with
  `py tools/progress.py --census <reimp_names.py output>`.
- **Phase 3, fixes:** fixes available in `src/fixes/fixes.c` over the
  defects catalogued as `### Dn.` in `docs/bugs.md`.

## What is estimated, and how to be honest about it

- Phase 1's completed work (translation, lockstep, routes, machine fidelity)
  is one item at 100%, backed by the parity evidence; each open roadmap item
  is an estimate with its gap named.
- An estimate is not raised for effort spent, only for something now shown
  (a gate that passes, a capture that matches, a defect found and fixed).
- When something thought done turns out not to be, its estimate goes down in
  the same commit that records the finding.
- Weights are revisited when the remaining work is understood better; a
  weight change is its own commit, so the percentages' jump is visible.
