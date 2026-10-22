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

### Phase 2: the gameplay score (21 October 2026)

Owner decisions of 21 October 2026. P2's "Matched functions replacing translations" item (weight 60) is
scored by the **gameplay score**, computed by `census_classify.gameplay()` during the usual
`progress.py --census ...` refresh and recorded as `census.gameplay` in progress.json:

- **Denominator:** every distinct instruction byte that any route executes, across all 17 game code files, from
  the validated interpreter captures of all routes (the `--route-cover` directory). Record validation is
  unchanged: rejected records (changed code, wrong file, undecodable) add nothing. The ten `err_*`
  damaged-install routes count like the others.
- **Numerator:** (a) bytes inside matched routine bodies (named cover) that a route executes; coverage only a
  fixture state reaches does not count; plus (b) executed bytes of routines that stay original **by design
  rule only**: STI-first interrupt handlers, the C start-up and its copies, and palette loaders with an STI in
  their loop. Inside the census these are the `docs/census-original.tsv` rows; outside it an entry must name
  its rule. Declined routines (VGAME main, MPS_LOGO `1ADC`/`1C82`, START `8427`, END `4393`, END `57B0`, and
  the rest) are remaining work, not original code. (b) counts only where a **reviewed reference entry**
  exists in `docs/reference-reviewed.tsv`. An entry counts only when a second reader has checked its name and
  purpose against the disassembly. Each range has exactly one status (`stays-original`,
  `machine-option-only`, `dead-certified`, `probable-dead`, `needs-route`, `data`, `unreached-held`). A
  `stays-original` entry over a census row outside `census-original.tsv` stops the refresh. The table starts
  empty, so (b) is 0 and the score starts at matched-only.
- **Published beside it:** the denominator in bytes; fixture-only matched bytes; executed but unmatched bytes
  by module (remaining work, which includes design-rule original code still waiting for its reviewed entry);
  and the **accounted-for checklist**. The checklist counts every byte of the previous all-code scope
  (204,747 by row occurrence, 204,677 distinct) that is not in the denominator, split by whether a reviewed
  entry (any status) covers it. The target is 100%.
- **Cross-checks on every refresh:** (a) and (b) are disjoint; a + b + remaining = denominator; denominator
  bytes inside the previous scope + checklist bytes = the previous scope's distinct bytes.

**The previous measure is kept.** The all-code measure below (credited over creditable census bytes plus the
supplement) is still computed and printed, labelled "previous measure". It still scores P2's "Routines named
and explained" item (weight 35). Naming and explaining is owed for the whole code inventory, including
reviewed static code and error paths that no route runs, which the gameplay score leaves out. Moving that
item too would score the same numerator twice. Reviewed reference entries do not count toward it yet; that
would be a separate decision. The tooling item (weight 5) is unchanged.

| 21 Oct 2026, batch am (`f3cbaff`) | previous measure | gameplay score |
|---|---|---|
| denominator | 204,747 creditable bytes (all-code scope) | 163,862 route-executed bytes |
| numerator | 165,370 named bytes (lockstep or routes) | 140,391 matched bytes a route runs + 0 reviewed original |
| share | 80.77% | **85.68%** |
| P2 / All | 81.73% / 94.42% | **84.67% / 95.32%** |

Current (22 Oct, batch as): **(142,864 + 1,826) / 163,862 = 88.30%**
(P2 86.67%, All 95.93%). The previous all-code measure is
**168,416 / 205,480**; 25,487 matched bytes run only in fixtures and earn no gameplay credit.
There are 19,172 unmatched executed bytes, including 20 design-rule original bytes awaiting a reviewed entry.
The largest remaining modules are ASOUND.117 5,373, ASOUND.LOG 2,615, RSOUND.117 2,213,
RSOUND.LOG 2,128, ISOUND.117 1,200, ISOUND.LOG 1,167, START 1,042 and MGRAPHIC 859.
The accounted-for checklist is 0 / 43,394; the full module and
checklist breakdowns come from `py tools/progress.py`. Per-batch measurements are in the
[roadmap](roadmap.md#batches-e-to-al-16-21-october-2026); the original gameplay-score measurement
is retained privately in `D:/f117-gate/gameplay-score-20261021/`.

### The previous all-code measure

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
  `docs/census-data.tsv`. `original` is the table `docs/census-original.tsv`: the STI-first timer and keyboard
  interrupts (VGAME 1132:006C keyboard handler, added 19 Oct: STI-first, never returns), the never-returning C start-up and its copies, the palette loaders with an STI inside their
  loop, and the divide-error handler no route reaches. When in doubt a byte stays in the denominator; a
  byte that a route ran or a matched body covers is credited whatever it was judged to be. A routine
  that is only hard, blocked by tooling or not yet proved belongs in the denominator.
  **`dead` (owner rule, 19 October: only code we are certain we do not need):** `docs/census-dead.tsv` lists 73 rows
  (2,927 never-run bytes) certified unreachable in the shipped game, each with a certificate; `docs/census-dead-ranges.tsv` adds byte ranges that are not census rows (range-keyed): 868 bytes of jump slots in SETUP and PLAYER that no program binds or names, and 367 bytes of code no walk decodes (alternate entries of the exit and file-open routines, unreferenced library functions, printf arms no format string uses), each read on 21 Oct
  (`D:/f117-gate/phase2-final/dead-cert/certificates.tsv`, criteria c1-c7: no direct reference in any program image,
  no pointer or computed reference, not an installed vector, every caller dead or a guarded arm whose guard cannot
  hold, no satisfiable path from route-executed code). By kind: unreferenced library and picture functions 1,634
  bytes, `printf` arms for conversions no format string uses (every format in the shipped programs is `%s` or `%d`)
  902, constant-argument arms 44, guarded signature hooks 40, exit epilogues after a call that never returns 18,
  alternate library entries 12, padding 8, empty `_initterm` tables 8. **The copy-protection quiz in START (633 bytes) is
  not listed** (owner, 20 October: functional but off by default): `--fix QUIZ` restores the call GOG patched out, so the
  quiz code is reachable and stays in the denominator; the routes `quiz_right` and `quiz_wrong` run it. Error paths
  (missing files, memory, usage, assert and fatal messages) are never listed: they stay in the denominator and are run on
  real routes instead (damaged-install routes). Only the never-run bytes leave: a byte a route ran or a body covers stays credited.
  **Executed code outside the census is included** (17 October 2026). The old census rows and
  their figures stay available for comparison. A separate `census.supplemental` count adds each byte
  outside them once when an interpreter route or a matched body's proof executes its instruction,
  across all 17 game code files (launcher, programs, resident graphics library and sound drivers). The score uses `(matched_bytes + supplemental.credited) /
  (creditable_bytes + supplemental.bytes)`. This was a denominator change, not lost matched work:
  P2 fell from 87.43% to 85.11% at the first correction. The route captures store four-byte
  windows, **not instruction lengths**, so the tool decodes each instruction, checks the module's
  file hash and its captured opcode/operand bytes against the image, allows relocated words and the
  known bound jump vectors, and rejects changed or undecodable code (317 unique records rejected
  against 64,512 accepted in the current 63-route captures; rejected records add no bytes). Fixture-only named bytes count by the same proof rule
  as inside the old census.
  **Reviewed static code** (`docs/census-code.tsv`, four tranches on 17-18 October): 37 ranges add
  11,491 bytes to the workload with no matching credit (text and DOS helpers, printf arms, arithmetic,
  pixel/span drawing, object spawning, picture wrappers, runtime handler shims, the complete VGAME
  `09EFA` body, the logo mouse helpers). Each row records segment/IP, length, instruction count,
  SHA256 and review evidence; every refresh verifies the hash, complete instruction boundaries,
  count and absence of overlapping reviews, and the private review also checks each instruction's
  length with Capstone. The reviews identify code to match; they do not claim a named equivalent.
  Lessons that shaped the rules: decoder agreement alone is insufficient (the logo mouse helpers'
  discovery lists overlapped at linear `1307`/`1308` until seven bounded walks resolved them);
  a range must not end inside an instruction (PLAYER `1AD4` stops at `1B9B`, before one that the
  census boundary bisects); canonical segment spellings avoid operand aliases (VGAME `0FB2:02B4`).
  Evidence: `D:/f117-gate/census-gap-20261017/`, `census-gap-20261018*/`.
  **Current total of this measure** (after batch am, the first matched routines outside the seven programs, and the 21 Oct module scope expansion): **165,370 / 204,747**, P2 **81.73%**, overall **94.42%** while it scored both P2 items (since the gameplay score, P2 84.67%, overall 95.32%) (2,927 never-run bytes certified dead left the denominator, 261 of them on 21 Oct, `docs/census-dead.tsv`; the 633 quiz bytes came back with `--fix QUIZ`) (P3 100.00% since the Stage 3 close).
  The historical census retains its recorded row boundaries, including overlapping
  positions: af adds 697 row-byte credits from 692 distinct instruction bytes
  (five positions occur in two rows). The separate raw-byte audit reports 690
  against the older route inventory; current captures supply the other two.
  `p2-batch-af-accept/credit-reconciliation.json` records both measures against
  the prior registry; the denominator and scoring method are unchanged.
  **Launcher and drivers included (21 Oct):** the approved scope expansion adds 18,835
  validated executed bytes across F117.COM, MGRAPHIC, MISC and six sound drivers. No matched
  credit changed: the denominator rose from 182,862 to 201,697, taking P2 from 90.85% to
  82.84% (the measured byte share is 81.93%; the completed tooling item supplies 5% of P2).
  All 17 game code files are eligible; NSOUND.117 currently contributes zero because no route
  runs it and it has no reviewed static range. On 21 Oct a fifth review tranche added 117 code ranges
  (4,481 bytes, 3,050 of them not already executed or named) and one 70-byte data range; the denominator
  rose from 201,697 to 204,747 and P2 fell from 82.84% to 81.68%, with no credit change. About 654
  static-discovery bytes in 42 groups are still held (the SETUP region that is XOR-transformed before it
  executes needs a strict validator first) and the shared floating LZEXE decompressor stays outside the measure. No new original-code
  or dead-code exclusion was made. Measurement and validation: `D:/f117-gate/modules-scope-20261021/`.
  `census_classify.py` prints the supplement, module counts and rejected-record counts;
  `progress.py --census ...` refreshes the historical and expanded counts together.
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
On 18 Oct 2026 the owner moved Stage 2's two open parts (line widths by drawing role, the physical-panel review) to Phase 4 as a "maybe": Stage 2 became 1.0 and P3 went to 97.79%.
