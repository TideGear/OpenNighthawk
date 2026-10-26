# Progress tracking

Every commit title starts with the current progress (it ended with it until
7 Oct 2026; older commits keep the suffix), for example
`(P1 100.00%, P2 67.82%, P3 96.33%, All 89.25%) Subject`, printed by
`py tools/progress.py --title`. `py tools/progress.py` alone prints every item
with its weight, completion and kind, and the Phase 2 module and checklist
breakdowns. This page says what the numbers mean, so that they can be checked
and argued with. What each item covers is in [roadmap.md](roadmap.md); the
current figures are quoted in [../handoff.md](../handoff.md).

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
roadmap's "Settled" list), so dropping work never reads as finishing it. Work
the owner removes from scoring altogether is not scored and stays listed as
possible later work: Phase 4 (what only a person or an independent reference
can settle, such as a person playing the game or Roland output against a
reference with a listening check; owner, 15 Oct 2026: its estimates had stopped
meaning anything) and the roadmap's "Possible future work" (17 Oct 2026).

## What is measured

Phase 2 has four items: tooling (weight 4.5, done), routines named and
explained (31.5, the previous all-code measure below), matched functions
replacing translations (54, the gameplay score) and the accounted-for
checklist (10). The first three were scaled together from 100 to 90 on
24 October 2026, keeping their proportions, when the checklist was added.

### Phase 2: the gameplay score (owner, 21 October 2026)

P2's "Matched functions replacing translations" item is scored by the
**gameplay score**, computed by `census_classify.gameplay()` during the usual
`progress.py --census ...` refresh and recorded as `census.gameplay` in progress.json:

- **Denominator:** every distinct instruction byte that any route executes, across all 17 game code files, from
  the validated interpreter captures of all routes (the `--route-cover` directory). Record validation is
  unchanged: rejected records (changed code, wrong file, undecodable) add nothing. The ten `err_*`
  damaged-install routes count like the others.
- **Numerator:** (a) bytes inside matched routine bodies (named cover) that a route executes; coverage only a
  fixture state reaches does not count; plus (b) executed bytes of routines that stay original **by design
  rule only**: STI-first interrupt handlers, the C start-up and its copies, palette loaders with an STI in
  their loop, and the program launcher (F117.COM `0100`..`03CA`, owner 23 Oct 2026: it only runs the other
  programs and tests their exit codes). Inside the census these are the `docs/census-original.tsv` rows;
  outside it an entry must name its rule. Declined routines (VGAME main, MPS_LOGO `1ADC`/`1C82`, START `8427`,
  END `4393`, END `57B0`, and the rest) are remaining work, not original code. (b) counts only where a
  **reviewed reference entry** exists in `docs/reference-reviewed.tsv`. An entry counts only when a second
  reader has checked its name and purpose against the disassembly. Each range has exactly one status
  (`stays-original`, `machine-option-only`, `dead-certified`, `probable-dead`, `needs-route`, `data`,
  `unreached-held`). A `stays-original` entry over a census row outside `census-original.tsv` stops the refresh.
- **Published beside it:** the denominator in bytes; fixture-only matched bytes; executed but unmatched bytes
  by module (remaining work, which includes design-rule original code still waiting for its reviewed entry);
  and the **accounted-for checklist**: every distinct byte of the previous all-code scope that is not in the
  denominator, split by whether a reviewed entry (any status) covers it. The target is 100%.
- **Cross-checks on every refresh:** (a) and (b) are disjoint; a + b + remaining = denominator; denominator
  bytes inside the previous scope + checklist bytes = the previous scope's distinct bytes.

When it was introduced (21 Oct 2026, batch am, `f3cbaff`) it read 85.68% against the previous measure's
80.77%, moving P2 from 81.73% to 84.67%; that measurement is retained privately in
`D:/f117-gate/gameplay-score-20261021/`.

**The previous measure is kept.** The all-code measure below (credited over creditable census bytes plus the
supplement) is still computed and printed, labelled "previous measure", and scores the "Routines named and
explained" item. Naming and explaining is owed for the whole code inventory, including reviewed static code
and error paths that no route runs, which the gameplay score leaves out; moving that item too would score the
same numerator twice. Second-read `stays-original` reference entries outside the census (the program launcher
and the SETUP timer handler, 790 bytes) leave its denominator as census-original rows do (owner, 24 Oct 2026;
naming 84.80% -> 85.12%, P2 81.31% -> 81.41% by that change alone); other statuses stay in it, and a byte a
matched body covers stays counted.

Per-batch credit is in the [roadmap's batch table](roadmap.md#batches-e-to-be-16-25-october-2026).

### Validated capture and reference corrections (24 October 2026)

- **The launcher's reference range** uses guest addresses `0100..03CA`; its earlier table start `0000` was a
  file-offset mistake, and correcting the COM origin recognized 249 executed launcher bytes under the existing
  rule. The loader at `03CB` stays outside that range. COM reference entries below `0100` fail validation.
- **Runtime-patched far jumps:** nine reviewed singleton jumps need runtime target operands (five timer chains,
  the keyboard chain and three logo sound-overlay thunks). `docs/census-runtime-jumps.tsv` pins each exact CS:IP
  and immutable file identity, records its original writer, and requires an original `EA 0000:0000` template plus
  captured EA opcode; only the target operand may change. This validates 17 capture records / 45 executed bytes
  without any matched credit. Other changed code, wrong identities and unreviewed singleton jumps stay rejected.
- **Rejected records** earn nothing merely by being rejected. After these corrections 300 remain: 112
  packed-bootstrap records, 187 unbacked zero captures from the damaged-driver route and one wrong-program
  VGAME/START record (64,529 accepted).
- **Handler reviews:** the original timer and keyboard reference entries now cover their final five-byte
  chain instructions. SETUP's static timer handler at `104D..10B3` (102 bytes / 51 instructions) starts with
  STI and has a second-read entry under that rule; its `10B3` retrace helper, PLAYER's handler (it starts with
  PUSH AX) and the three logo thunks remain matching work.
- **The launcher loader's six error blocks** have second-read entries (128 bytes already covered only by
  matched fixtures, plus the two-byte DOS termination interrupt at `04E3`, now a strictly hashed static-code
  row). Allocation/resize failures are machine-option-only; overlay-load/header failures and the common exit
  need routes; the successful FFFF-paragraph allocation arm stays held until its allocator and possible
  indirect entries are closed. None of this grants matching credit or a dead-code exclusion.

### Phase 2 completion requirements (owner, 23-24 October 2026)

The accounted-for checklist is the fourth measured item: reviewed bytes divided by all previous-scope bytes
outside the gameplay denominator. Adding it lowered the headline P2 score from 90.04% to 81.04%; no matching
credit was lost. Every entry needs a supported status, purpose, evidence and a second reader. An entry marked
`probable-dead`, `needs-route` or `unreached-held` records a review but does not close that code's work.

P2 is done only when all of the following hold:

- Every validated route-executed byte is matched or covered by a second-read original-by-design
  entry; all declined routines remain work until proved. No difficulty-based exclusions.
- The entire all-code inventory is named and matched, except code covered by an existing design
  rule or a strict dead/data certificate. Fixture proofs cover machine-option-only errors,
  including out-of-memory, DOS file failures and the `_C_FILE_INFO` environment arm.
- Every byte outside routes has a second-read reference entry, and each unresolved classification
  has been resolved by a proof, a route or a strict certificate. The probable-dead bytes cannot
  close on probability alone.
- Static discoveries are fully reviewed and adopted with strict validation. Encoded code needs
  pinned file/region/decoded hashes, transform bounds and exact instruction counts before
  adoption; no decoded binary is published. (The two encoded SETUP ranges were adopted this way on
  24 Oct 2026: `tools/encoded_code.py`, `docs/census-encoded-code.json`, 585 bytes of workload with no
  matching credit.)
- The packer stub is measured and independently reviewed under an explicit design rule before
  receiving any credit; the design-rule name is recognized but grants nothing yet. Its records belong
  to the packed representation, so their addresses must never be credited as original code in the
  unpacked image.
- A final closure review covers indirect calls, jump/dispatch tables, callbacks, interrupt vectors,
  overlay entry tables and self-modifying code, with evidence and unresolved findings recorded.
  This is a required closing step, not a percentage of undiscovered code.
- All required proof, mutation, full parity, event-limit and presentation gates pass for the final
  code, and the documentation and published revision agree with the accepted evidence.

A rounded display of 100.00% is insufficient if any bytes or closing requirements remain.

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
  count alongside routes, as for the proofs themselves. Census rows keep their
  recorded boundaries, so a byte in two rows earns two row-byte credits (batch
  af: 697 credits from 692 distinct bytes, `p2-batch-af-accept/credit-reconciliation.json`).
  Refresh after every batch: run `func_lockstep` with `F117_NAMED_COVER=FILE`
  (one file per shard in a directory; it appends one line per matched routine),
  then `py tools/progress.py --census <reimp_names.py output> --named-cover DIR
  --route-cover <interpreter captures of all routes> --data <game directory>`.
  The refresh stops if any matched routine has no coverage line.
  **The denominator is split** (owner, 17 October 2026): the score is credited bytes over the *creditable*
  census, which leaves out the bytes that can never earn credit. `tools/census_classify.py` puts every census
  byte in one class: `credited`; `ran` (a route ran it, no matched body covers it yet); `never_run` (decodes
  as code reachable from a census entry, but no route runs it); `unreached` (no walk reaches it and nothing
  marks it as data); `slots` (unfilled entry-point jump vectors, `EA 00 00 00 00`, that the program's segment
  binder fills at start-up: live code, so they stay in the denominator); `data`; `original`; and `dead`. Only
  `data`, `original` and `dead` leave the denominator, and only the bytes of those classes that are not
  credited. `data` needs strong evidence: a zero fill of eight bytes or more, a text run of eight or more that
  is at least 80% letters and spaces and that no walk decodes as code (opcodes are printable too: `cmp al,'s'`
  reads as text), or a range a person has read and listed in `docs/census-data.tsv`. `original` is the table
  `docs/census-original.tsv`: the STI-first timer and keyboard interrupts (VGAME 1132:006C keyboard handler,
  added 19 Oct: STI-first, never returns), the never-returning C start-up and its copies, the palette loaders
  with an STI inside their loop, and the divide-error handler no route reaches. When in doubt a byte stays in
  the denominator; a byte that a route ran or a matched body covers is credited whatever it was judged to be.
  A routine that is only hard, blocked by tooling or not yet proved belongs in the denominator.
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
  **Executed code outside the census is included** (17 October 2026). A separate `census.supplemental` count
  adds each byte outside the census rows once when an interpreter route or a matched body's proof executes its
  instruction, across all 17 game code files (launcher, programs, resident graphics library and sound drivers).
  The score uses `(matched_bytes + supplemental.credited) / (creditable_bytes + supplemental.bytes)`. The route
  captures store four-byte windows, **not instruction lengths**, so the tool decodes each instruction, checks the
  module's file hash and its captured opcode/operand bytes against the image, allows relocated words, the known
  bound jump vectors and the reviewed runtime-patched jumps above, and rejects changed or undecodable code
  (rejected records add no bytes). Fixture-only named bytes count by the same proof rule as inside the census.
  **Reviewed static code** (`docs/census-code.tsv`, tranches from 17 October) adds code no route runs to the
  workload with no matching credit (text and DOS helpers, printf arms, arithmetic, pixel/span drawing, object
  spawning, picture wrappers, runtime handler shims, the complete VGAME `09EFA` body, the logo mouse helpers).
  Each row records segment/IP, length, instruction count, SHA256 and review evidence; every refresh verifies the
  hash, complete instruction boundaries, count and absence of overlapping reviews, and the private review also
  checks each instruction's length with Capstone. The reviews identify code to match; they do not claim a named
  equivalent. Lessons that shaped the rules: decoder agreement alone is insufficient (the logo mouse helpers'
  discovery lists overlapped at linear `1307`/`1308` until seven bounded walks resolved them); a range must not
  end inside an instruction (PLAYER `1AD4` stops at `1B9B`, before one that the census boundary bisects);
  canonical segment spellings avoid operand aliases (VGAME `0FB2:02B4`). Evidence:
  `D:/f117-gate/census-gap-20261017/`, `census-gap-20261018*/`. The strictly validated encoded SETUP code is
  included by physical byte union; static discoveries not yet reviewed and the shared floating LZEXE
  decompressor stay outside the measure.
  **Denominator changes, none of which removed matched credit:** executed code outside the census
  (17 Oct: P2 87.43% -> 85.11%); the launcher, MGRAPHIC, MISC and six sound drivers (owner, 21 Oct: 18,835
  validated executed bytes, denominator 182,862 -> 201,697, P2 90.85% -> 82.84%; measurement and validation in
  `D:/f117-gate/modules-scope-20261021/`); a fifth static review tranche (21 Oct: 117 code ranges and one 70-byte
  data range, 201,697 -> 204,747, P2 82.84% -> 81.68%). All 17 game code files are eligible; NSOUND.117
  contributes zero because no route runs it and it has no reviewed static range. `census_classify.py` prints
  the supplement, module counts and rejected-record counts; `progress.py --census ...` refreshes the historical
  and expanded counts together.
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
  2's open share fell by the coastline item, 0.95 to 0.967, and P3 went to
  96.62%; moving the refresh rates above 60 Hz to Phase 4 (Stage 3, 0.9 to
  0.933) made it 97.22%. On 18 Oct 2026 the owner moved Stage 2's two open
  parts (line widths by drawing role, the physical-panel review) to Phase 4 as
  a "maybe": Stage 2 became 1.0 and P3 went to 97.79%. On 24 Oct 2026 the
  owner added the re-release features (roadmap, Phase 3) as unstarted items
  weighing 28 beside the existing 100, in two commits: P3 went from 100% to
  89.29% and then 78.12%; hybrid sound (weight 3) the same evening took it to 76.34%. The same day the Phase 2 checklist item was added
  (above).
