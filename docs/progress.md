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
roadmap's "Settled" list), so dropping work never reads as finishing it; the
exception is work the owner removes from scoring altogether (Phase 4, 15 Oct
2026; the items under "Possible future work" in the roadmap, 17 Oct 2026),
which is not scored and stays listed as possible later work.
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
  --route-cover <interpreter captures of all routes> --data <game directory>`.
  The refresh stops if any matched routine has no coverage line.
  **The denominator is split** (owner, 17 October 2026): the score is credited bytes over the *creditable*
  census, which leaves out the bytes that can never earn credit. `tools/census_classify.py` puts every census
  byte in one class: `credited`; `ran` (a route ran it, no matched body covers it yet); `never_run` (decodes
  as code reachable from a census entry, but no route runs it); `unreached` (no walk reaches it and nothing
  marks it as data); `slots` (unfilled entry-point jump vectors, `EA 00 00 00 00`, that the program's segment binder fills at
  start-up: live code, so they stay in the denominator);
  `data`; and `original`. Only `data` and `original` leave the denominator, and only the bytes of those
  classes that are not credited. `data` needs strong evidence: a zero fill of eight bytes or more, a text
  run of eight or more that is at least 80% letters and spaces and that no walk decodes as code (opcodes
  are printable too: `cmp al,'s'` reads as text), or a range a person has read and listed in
  `docs/census-data.tsv`. `original` is the table `docs/census-original.tsv`: the STI-first timer
  interrupts, the never-returning C start-up and its copies, the palette loaders with an STI inside their
  loop, and the divide-error handler no route reaches. When in doubt a byte stays in the denominator; a
  byte that a route ran or a matched body covers is credited whatever it was judged to be. A routine
  that is only hard, blocked by tooling or not yet proved belongs in the denominator.
  **Executed code outside the census is now included** (17 October 2026). The old census rows and
  their figures stay available for comparison. A separate `census.supplemental` count adds each byte
  outside them once when an interpreter route or a matched body's proof executes its instruction,
  within the same seven programs. The score uses `(matched_bytes + supplemental.credited) /
  (creditable_bytes + supplemental.bytes)`. Initially this adds 5,927 bytes, 846 already named:
  148,294 / 175,860 creditable bytes, P2 85.11% and overall 94.75%, versus 87.43% and 95.46%
  before the scope correction. This is a denominator change, not lost matched work.
  The route captures store four-byte windows, **not instruction lengths**: the tool decodes the
  instruction, checks the module's file hash and its captured opcode/operand bytes against the image,
  allows relocated words and the known bound jump vectors, and rejects changed or undecodable code.
  The 127 rejected unique records are reported alongside 57,646 accepted records; they add no bytes.
  Fixture-only named bytes count by the same proof rule as inside the old census. Static discoveries
  without execution evidence are still an audit backlog. The launcher, resident graphics library and
  sound drivers have 18,469 validated executed bytes reported separately; extending matching and the
  score to these modules remains open. Thus the score measures the expanded seven-program scope,
  not all game code. `census_classify.py` prints the supplement, module counts and rejected-record counts;
  the existing `progress.py --census ...` command refreshes both old and expanded counts together.
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
  4K ("good at least for now", 15 Oct, 22:32) Stage 3 went to 0.9: P3 96.33%. On
17 Oct 2026 the owner removed the work he had decided against from the score
(roadmap, "Possible future work" and the coastline item under Phase 4): Stage
2's open share fell by the coastline item, 0.95 to 0.967, and P3 went to 96.62%; moving the refresh rates above 60 Hz to Phase 4 (Stage 3, 0.9 to 0.933) made it 97.22%.
