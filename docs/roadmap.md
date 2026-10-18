# Roadmap

What is done and what is left. The game's own bugs are in [bugs.md](bugs.md);
how parity is built and checked is in [architecture.md](architecture.md); the
commands, current figures and traps are in [../handoff.md](../handoff.md); the
scoreboard is `py tools/progress.py` ([progress.md](progress.md)). Update this
page with every piece of work, and keep it short: one entry per item, the
evidence linked or in the commit, not narrated here.

The order of the phases is the project's: **1:1 parity first** - the same
player actions playing out as on the original's hardware - then understood
(named) code, then fixes and enhancements - each switchable, never
instead of parity - and last the checks only people and outside references can
make (Phase 4).

## Phase 1 - recompiled, 1:1 with the original

### Done

- **Translation.** All 17 code files and the LZEXE decompressor: 89,365
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). Changed code and declined override
  entries fall back to the interpreter.
- **Engine parity.** 45 scripted routes (44 sessions) are identical between
  the interpreter and the recompiled code at every 50-million-clock checkpoint
  and at the end; every translated instruction is held to the interpreter from
  random states (5,718,848 comparisons under each of the two timing profiles,
  0 differences); the interpreter and the translator are held to 8088 and
  80286 silicon vectors; 144 of 144 planted defects are caught; seeded random
  flights are identical. How:
  [architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence).
- **What the routes cover.** All nine theatres, three tensions and four mission
  categories; all eight primary objective types with normal-input routes and
  strong observers (reconnaissance, ground strike, supply drop with the original
  D5 no-credit behaviour and `cargo_d5_fixed`, secret airstrip, air-to-air types
  5-8), with verified return legs for types 1-4; landing; the speaker and Roland
  driver boots; pilot creation, editing and erasure, KIA and retired dialogs and
  direct maintenance through the original UI; the earned tenth-mission tour
  ribbon, promotion and Airman's Medal; a rank-3 career paired through
  retirement at sortie 99; the rank-6 "General, At Last!" retirement reached
  with a staged roster (`tools/career_rank6.py`; not an earned rank-6 career).
  Per route: [../tools/routes/README.md](../tools/routes/README.md).
- **The emulated PC against GOG's DOSBox 0.74** (`tools/fidelity.py`): 1,210
  comparable answers agree. The machine follows DOSBox where it was measured
  to (memory layout, VGA timing and scanout, DAC, the PIT control-word
  interrupt, ISA I/O delay, the mouse cursor drawn into video memory):
  [architecture.md](architecture.md#time) and
  [The PC](architecture.md#the-pc).
- **Pictures and music against GOG's DOSBox.** `tools/video_compare.py`: 1,329
  exact pictures in order against the saved capture; three one-sample
  logo-transition pictures are unmatched on each side, and a second capture
  matches 1,321 with 11 per side, so the remainder varies by capture. Music:
  all 22,840 AdLib register writes over the logo and intro identical in order
  and value, timing -14 to +1 ms (`tools/dosbox_compare.py`).
- **Other machines as references** (`tools/pc_parity.py`): DOSBox-X matches
  1,237 exact pictures in order (drift up to 0.2 s); 86Box (a 386DX/33 with
  MS-DOS 5.00 and MOUSE.COM 6.26) matches 85 of 86 graphics pictures in 6-bit
  DAC values (the other is START's roster screen sampled mid-draw). The same
  run checks the intro music (GOG envelope 0.963, spectral 0.992; 86Box's first
  2,000 AdLib writes identical) and a scripted START session's saved roster
  (byte-identical on DOSBox-X and 86Box); `tools/fidelity_all.py` holds the
  machine probe to stored baselines. Method:
  [architecture.md](architecture.md#verification-why-11-is-a-claim-with-evidence),
  layer 8; builds: `tools/ref86box/`.
- **Independent GOG DOSBox flights.** Reconnaissance with both photos and the
  raised-deck return completes on GOG's DOSBox and passes the same strict
  gates. The supply mission is reproduced exactly there (the seed is set 275 ms
  early for its autoexec); delivery there is not pursued (Settled, below).
- **The game version verified:** GOG's installer (build 28044) with the 473.04
  update; Steam's release is byte-identical (`tools/verify_install.py`).
- **The gate** (`tools/build_recomp.py`): translate, build, unit tests (238),
  coverage, both engines on every route, instruction lockstep under both
  timing profiles, matched-routine lockstep at three seeds, and the
  routes-only and event-limit checks. About 21-33 minutes when every
  interpreter route is verified fresh (latest 1,557 s); costs and what was
  optimised: [repeated-processes.md](repeated-processes.md).
- **Rendered speaker and speech audio.** PIT counter 2 follows the 8254 model;
  speaker music matches DOSBox-X (envelope 0.87, spectral 0.92-0.93, level
  0.95). Against a silent capture of 86Box's mixer (MS-DOS 5.00, MOUSE.COM
  6.26) the takeoff call's 12,924 non-silence counts are identical and the
  300-3400 Hz waveform correlates at -0.9916 (opposite polarity, as the
  independent speaker models' formulas give), envelope 0.9995, zero lag; no
  renderer change was needed. Commands and limits:
  [build_86box.md](../tools/ref86box/build_86box.md#silent-speech-waveform-reference).
  Listening is Phase 4; sample-for-sample OPL equality is out of scope.
- **A 386DX/33 timing profile** (10 Oct 2026; `f117run --timing 386`,
  `F117R_TIMING=386`; reference
  [timing386.md](../tools/ref86box/timing386.md)). The game steps its flight
  model by its own frame rate and reads the stick once a frame, so the same
  inputs play out differently on a faster or slower PC; this profile makes the
  recompilation play like the original on a period machine. Recorded routes
  stay on the default (DOSBox) profile; results that do not depend on speed
  must match under both.
  - *CPU and services:* the interpreter matches 86Box to the cycle on all 77
    blocks of `probe386.py`; natively answered BIOS, DOS and mouse services
    are charged their measured cost (all 93 probe blocks exact against MS-DOS
    5.00 with MOUSE.COM 6.26; 5.00 and 6.22 cost the same on the game's calls,
    `build_msdos_vm.py`).
  - *Disk:* DOS file services fitted to the reference's period drive; PLAYER,
    DSWAP and START loads charged at their measured disk positions. The intro
    ends +0.04 s from 86Box (+0.43 s uncharged).
  - *Result:* the game's frame rate in flight (S) is 5-9 under the profile and
    7-9 on 86Box, against 14-15 on DOSBox's model; the intro's scenes hold
    86Box's pace to a frame; 21 of 21 held pictures match (duration difference
    at most 0.214 s). Both engines charge the profile: the gate's 386
    instruction lockstep, and 574 identical paired flight checkpoints
    (`stick_response.py --machine machine386`). Matched routines run the
    original translated body under this profile.

### Settled (scope, 8 Oct 2026)

Closed because what is left measures a reference emulator or the test tools,
not the recompilation:

- [x] **Frame parity with DOSBox.** 1,329 of 1,332 pictures exact; the rest
      are one-sample transitions whose frame varies with the capture's phase.
      Frame timing against a period PC is the 386 profile's job.
- [x] **86Box and DOSBox-X as routine checks.** `pc_parity.py` and
      `fidelity_all.py` are the routine checks; the closed-loop supply drop,
      strike-training hit and career sortie pass on both. The test pilot
      passes 7 of 9 over a +-15% range of its stick scale on 86Box: a property
      of the pilot, not of either machine.
- [x] **Timing details still DOSBox's own** (its BIOS and DOS stubs, its
      per-millisecond slicing, the 275 ms boot phase, `--boot-ms`). They
      describe DOSBox, not the original; the 386 profile supersedes them. The
      game's PUSHF sites never read the 286/386 flag bits.
- [x] **Individual mission objectives.** All eight primary objective types
      have gate routes replayed byte-identically by both engines; the
      closed-loop pilots deliver on DOSBox-X and 86Box (GOG's DOSBox replay
      diverges open-loop and is not pursued); the strike-training credit window
      is the original's (80-150 map units, bugs.md D7).
- [x] **The career flow.** Promotion, medals, ribbons, retirement and the
      rank-6 branch are reached and paired (rank 6 from a prepared roster:
      END's promotion code is the same however the roster was made). END's
      tables, read from its memory: totals 300, 1,125, 3,000, 7,000, 16,000,
      27,720; averages 100, 150, 200, 250, 280, 280; sorties 2, 5, 10, 20, 40
      and exactly 99; score multipliers from the pilot-skill form (one sortie:
      168 Green, 231 Regular, 294 Veteran, 288 Regular with Realistic
      Landings). An earned career was flown to sortie 10 at an average of 290
      (`tools/career_chain.py`) and stopped: 89 more scripted sorties would
      test the pilot, not the game.

## Phase 2 - understood code

- [ ] **Name the translated routines and data**, with explanations, drawing on
      the Reimp's mapping; matched functions replace translations one at a
      time, each held to the same parity checks. Method:
      [architecture.md](architecture.md#matched-routines-phase-2); the table is
      at the end of `src/matched/matched.c`; how routines are proved now (the
      private kit and parallel agents) and the next candidates:
      [../handoff.md](../handoff.md). `tools/reimp_names.py` joins the Reimp's
      census (1,535 functions) with this project's regions as a private lead
      list.
      **Now:** 1,006 addresses matched; named code runs 147,448 of the
      169,933 creditable census bytes (179,213 in all, 9,280 of them data or code that stays original; see
      [progress.md](progress.md#what-is-measured)): 964 census rows
      wholly, 293 in part. Credit is by byte since 16 October
      ([progress.md](progress.md#what-is-measured)): under it master's 946 routines
      held 132,537 bytes (132,638 by whole rows), batch g added 6,181 and
      batch h added 3,894, batch i 602, batch j 1,349, batch k 802, batch l 314, batch m 262, batch n 1,239 and batch o 268.
      **Expanded scope (17 October):** add 5,927 bytes outside the census whose instructions routes or matched
      proofs execute in these seven programs, 846 already named, then 8,341 reviewed static code bytes in
      16 ranges (`docs/census-code.tsv`: hashes, complete instruction boundaries, counts and review evidence).
      The score now measures 148,294 / 184,201 creditable bytes (P2 81.48%, overall 93.64%); the earlier
      87.43% used only the old census. No matching was removed. The remaining 7,089 bytes in the static
      inventory need review; speculative table/padding decodes and independent-decoder disagreements
      prevent blindly adding every translated byte. These and 18,469 validated executed bytes in other modules
      remain an explicit scope backlog. The route-byte audit now decodes instruction lengths instead of
      counting four-byte capture windows; hash/image validation rejects changed code. All 57,646 accepted
      records agree with an independent decoder. VGAME `09EFA` is screened: 1,628 instructions / 4,552
      bytes, no observer or fix at its entry; the private dossier is prepared, implementation/proof still open.
      **What is left:** `tools/census_reach.py` separates code with run
      evidence from code without (gate coverage logs only interpreted
      fallback instructions, so it cannot show what ran). Fresh interpreter
      captures of all 45 routes (16 Oct, after batch d) run the entries of
      381 unmatched census rows / 25,821 bytes, including 92 rows / 7,688
      bytes that start inside one of our translation regions (interior
      entries such as VGAME `0CB77` key dispatch, which the tool used to
      skip as data). 21 rows / 4,764 bytes have only interior instructions
      run, and 208 rows / 18,901 bytes have no evidence; those include data
      (DSWAP `3DF3`, SETUP `27AE`'s strings) and need new routes or a
      data/code decision each.
      **What the old census's uncredited bytes are** (17 Oct, corrected instruction lengths,
      `tools/census_classify.py`, 31,765 bytes): 5,486 in instructions routes ran (workable now),
      15,127 decoded as code that no route runs (needs a new route or a decision that it is unreachable),
      1,030 that no walk reaches and nothing marks as data (to be read),
      842 in entry-vector slots no route uses (SETUP `2D65`, PLAYER `3BFA`: the program's segment binder,
      VGAME `0FAE:0002`, fills them with far jumps at start-up; 47 of VGAME's 84 run, so they are live code and
      stay in the denominator), and the 9,280 bytes of data and original code that left it.
      The classifier's cross-checks: no excluded byte lies in a row our translation treats as code, and
      the one text rule that hid real code (PLAYER `1BE7`, a printf switch of compare-and-jump bytes that
      reads as text) was tightened; every range in `docs/census-data.tsv` was read in full.
      **Stays original code:** the timer interrupts (VGAME 0x1C9B, START
      0x8D0B, END 0x4BEF) open with STI, after which the run loop admits one
      more instruction before it looks at events, so a matched body could do
      nothing the original does not. The line clipper's divide-error handler
      (`0FB2:04DD`) is fixture-proven but **not integrated**: no input reaches
      it (146,153 real clip divisions across 14 routes stay well under its
      32,767-unit threshold); it is kept on local branch `irq-divide-fault`
      until a route reaches it. The C start-up routine (START `8EDC` and its
      six copies) must stop right after its own STI and never returns to its
      caller's stack, so no lockstep state can complete it; it needs a harness
      completion rule first (`D:/f117-gate/p2-start-8edc/BLOCKED.txt`); its
      copies are VGAME `E6BE`, DSWAP `0C7E`, SETUP `1516`, END `4DD2`, PLAYER
      `1096` and MPS_LOGO `0146:0016`. The far palette loader VGAME `14C2:0000`
      has STI inside its colour loop, like START `8378` and END `42E4`, whose
      matched bodies stop at the first STI.
      Excluded as unequal: START 0x11DB, MPS_LOGO 0x1ADC and 0x1C82.

### How an entry is proved

Each entry below is compared with the original from random states at several
seeds (zero differences, every instruction reached by the named code unless
stated), rejects deliberately broken bodies ("mutations"), and is run by a
real route with every checkpoint and the final state exact. Every batch then
passed a fresh full gate before its commit. Per-seed state counts and gate
times are in the commits, the named `D:/f117-gate/` evidence directories and
[repeated-processes.md](repeated-processes.md#phase-2-proofs-matched-routines).
Real-route checkpoint counts: `full_cycle` 66, camera-gradient replay 234, F3
replay 57, boot/startup 54, training 133. *Routes only* marks a routine no
random state can compare (interrupt-vector or BIOS calls), proved by the
routes alone; *thin* marks one a real route calls only once or a seed barely
reaches. Candidate drafts can keep private static dossiers
(`matched_draft.py --dossier`):
[repeated-processes.md](repeated-processes.md#phase-2-candidate-reading).

### Latest batch: report text, view and sky, formatter dispatch (17 October 2026, batch o)

Batch o adds three addresses: 1,006 addresses, 268 more census bytes. Five
seeds each reach every instruction of the three bodies (111 in all); all 104
mutations are caught. END proves on the exact `recon_career` route (344
checkpoints, 1 call); VGAME and START on the exact `full_cycle` route (66
checkpoints, 276 and 89 calls); fifteen optimized seed runs pass. Evidence
`D:/f117-gate/p2-batch-o-accept`.

- END `26A9`, `end_report_page_text`: the continue-prompt screen of the
  debrief: restore the text area from page 1, draw the wrapped text, wait at the prompt.
  44 instructions / 90 bytes, 27 mutations.
- VGAME `D826`, `vgame_set_view_and_sky`: set the view, camera matrices and
  scene origin, then draw the sky bands when asked (the wait on the timer flag
  runs only on the route; lockstep delivers no interrupt). 30 instructions /
  86 bytes, 37 mutations.
- START `9E3E`, `start_format_dispatch`: the printf formatter's conversion
  dispatch (an interior arm with eleven exits). 41 instructions / 92 bytes,
  40 mutations. The lockstep harness now lets any `FATAL_JUMPS` row of a
  routine end its named code; END `400F` (DOS open with a fatal tail) was
  proved privately but is held back until its tail is verified.

### Latest batch: the debrief scorer and a packed picture opener (17 October 2026, batch n)

Batch n adds one address and expands an existing one: 1,003 addresses,
1,239 more census bytes. Five seeds each reach all 646 instructions across
both bodies; all 199 mutations are caught, and fifteen optimized seed runs
pass (five of them on the existing END `34EA` entry). Evidence
`D:/f117-gate/p2-batch-n-accept`.

- END `3021`, `end_debrief_log_score`: the complete scorer with its
  12-way switch (579 instructions / 1,608 bytes, 152 mutations). The registered
  `34EA` entry is preserved as an alias that resumes after the increment;
  the initial jump to `34ED` does not increment. Exact `recon_career`
  (344 checkpoints, 3 calls) and a 26-checkpoint boot proof.
- VGAME `11B8:0008`, `vgame_picture_open_far`: the far picture opener with
  its terminal DOS-error exit (67 instructions / 152 bytes, 47 mutations).
  The harness gained terminal-entry (`PROGRAM_ENDS`) and native INT 10h
  support for it. Exact `full_cycle` (66 checkpoints, 2 calls).

Fresh evidence replaces the inherited `3021`/`34EA` opening coverage.
The census denominator is unchanged; switch data `34D2..34E9` stays uncredited.

### Awards page callback and complete ribbon collector (17 October 2026, batch m)

Batch m adds one address and expands an existing one: 1,002 addresses,
262 more census bytes. Five seeds each reach all 138 instructions across
both bodies; all 74 mutations are caught. Each has one call on the exact
344-checkpoint `recon_career` route and a 26-checkpoint boot proof; ten
optimized seed runs pass. Evidence `D:/f117-gate/p2-batch-m-accept`.

- END `2200`, `end_awards_page_callback`: compose the awards page with
  three full-page driver copies, the ribbon and report callbacks. All
  arguments, register setup, returns and caller cleanup are preserved.
  52 instructions / 101 bytes, 28 caught mutations.
- END `22EB`, `end_collect_award_ribbons`: replace the existing opening
  with the complete ribbon collector and draw loop. Scan six then five
  award words, append up to three further indices within twelve slots,
  and draw them through the original row helper. Offset wrap, byte
  counters and the DS-based clear when SS differs are preserved.
  86 instructions / 235 bytes, 46 caught mutations. An equivalent
  comparison-width mutation was replaced; the first attempt is retained.

Fresh evidence replaces the old END `22EB` opening: +161 bytes there,
+101 from the callback. No duplicate entry or credit is added; the same
census TSV and denominator are retained.

### A far picture wrapper and a complete grid formatter (17 October 2026, batch l)

Batch l adds one address and expands an existing one: 1,001 matched addresses,
314 more census bytes. Both bodies have five seed proofs, full instruction
coverage, 115 caught mutations in all, exact real-route and boot proofs, and
ten optimized seed runs. Evidence `D:/f117-gate/p2-batch-l-accept`.

- VGAME `11D8:000A` (linear `11D8A`), `vgame_pic_rows_to_page_far`: the far
  picture/page wrapper, 40 instructions, 41 mutations, `full_cycle` 2 calls
  and 66 exact checkpoints. Its 200-row loop reads the relocated DS operand
  and driver call targets from live code. Probes fold return IP/CS as well
  as registers; a first missed return-address mutation was preserved, then
  all seeds and mutations were repeated with the strengthened probes.
- START `58DF`, `start_onc_grid_reference`: the full nine-arm theatre grid
  formatter replaces the former six-instruction opening at the same address.
  91 instructions, 74 mutations, `korea_strike` 2 calls and 76 exact
  checkpoints. The fixture permutes the live switch table and covers signed
  coordinates and the original uninitialized-bias behavior above theatre 8.
  END's shared opening helper is retained; no duplicate START entry is added.

Fresh coverage replaces the old START opening's evidence: +197 bytes there,
+117 for the picture wrapper. The eighteen switch-table bytes `5963..5974`
and VGAME `11DFF` stay uncredited; the same TSV/denominator is retained.

### 5 routines (17 October 2026, batch k)

Batch k reaches 1,000 matched addresses and adds 802 census bytes. Five seeds
per routine reach every instruction; all 267 mutations are caught. Exact
real-route and boot proofs pass, as do 25 optimized seed runs. The final
private proofs were repeated against batch j's normal optimized libraries;
source and library hashes are frozen in `p2-batch-k-accept/refresh-verdict.json`.

END `3BF4` includes the shared timer-install tail `4B8C` (235 bytes in two
ranges); its wide test envelope also includes nine fixture callee JMPs, so
95 covered instructions represent 86 original instructions. Code-operand
stores keep separate stop boundaries. SETUP `111A` adds one `POPF_STOPS`
row, using the existing completion rule. Its unreachable gap at `1182`
remains uncredited; the census denominator is unchanged.
Evidence `D:/f117-gate/p2-batch-k-accept`.

- END `3BF4` prepare drivers memory devices report pages and the clock hook: 86 original instructions (95 including fixture probes), 58 mutations, `full_cycle` 1 call (`p2-end-3bf4`).
- SETUP `111A` set the timer rate from the refresh: 54 instructions, 52 mutations, `setup_calibration` 2 calls (`p2-setup-111a`).
- END `2265` run the awards report pages and announcements when enabled: 46 instructions, 52 mutations, `full_cycle` 1 call (`p2-end-2265`).
- END `1BAA` wait for report widget selection cancel or matching input: 58 instructions, 50 mutations, `full_cycle` 1 call (`p2-end-1baa`).
- START `3EA5` wait for confirm or cancel through buttons or keyboard: 56 instructions, 55 mutations, `career_serge` 2 calls (`p2-start-3ea5`).

### 4 routines (17 October 2026, batch j)

Batch j adds four routines and 1,349 census bytes. Five seeds per routine
reach every instruction, all 203 mutations are caught, and exact real-route
and boot proofs pass. All 20 optimized seed runs pass. END `0010`'s final two
bytes remain uncredited because the fixture patches the following callee.
Evidence `D:/f117-gate/p2-batch-j-accept`.

The first gate exposed a pre-existing START `76C0` RNG error: the generator
can update its caller's divisor through aliased memory. The named routine
now reads the bound after that call, as the original does. Five private
seeds, nine-instruction coverage, 28 mutations, exact boot/route proofs and
six optimized generic seed runs prove the correction; the failing seed is
retained as `matched_start_rnd_stack_alias`. The full gate was rerun.

- START `6AB3` edit a pilot name with prefix backspace acceptance and roster update: 254 instructions, 64 mutations, `roster_edit` 3 calls (`p2-start-6ab3`).
- END `2703` announce granted and deferred medals special awards and service ribbons: 196 instructions, 69 mutations, `recon_career` 1 call (`p2-end-2703`).
- START `0010` run the front end prepare the mission block and choose its exit code: 40 instructions, 33 mutations, `full_cycle` 2 calls (`p2-start-0010`).
- END `0010` run the debrief update sorties and terminal pilot status then shut down: 51 instructions, 37 mutations, `full_cycle` 1 call (`p2-end-0010`).

### 6 routines (17 October 2026, batch i)

Batch i adds six routines and 602 census bytes. Each has five seed proofs
with every instruction reached, 207 mutations caught in all, an exact boot
proof (26 checkpoints) and an exact `full_cycle` proof (66 checkpoints) that
calls it. All 30 optimized seed runs pass too. START `9094` gains a
`PROGRAM_ENDS` row and a return rule for its alternate-entry stack layout.
PLAYER `019A`'s returning fixture covers six terminal cleanup bytes; the last
two stay uncredited by the named-byte measurement.
Evidence `D:/f117-gate/p2-batch-i-accept`.

- END `3F44` shut the drivers and hooks down and exit with the code: 36 instructions, 42 mutations, `full_cycle` 1 call (`p2-end-3f44`).
- START `8782` unpack a compressed picture onto a page row by row: 38 instructions, 38 mutations, `full_cycle` 17 calls (`p2-start-8782`).
- PLAYER `019A` wait for the last picture (and the sound driver), then finish: 36 instructions, 31 mutations, `full_cycle` 2 calls (`p2-player-019a`).
- END `4648` decode a picture onto a page row by row: 38 instructions, 32 mutations, `full_cycle` 4 calls (`p2-end-4648`).
- VGAME `0FB2:0246` fill a box through the span tables: 45 instructions, 39 mutations, `full_cycle` 304 calls (`p2-vgame-fd66`).
- START `9094` end the program: 43 instructions, 25 mutations, `full_cycle` 1 call (`p2-start-9094`).

### 18 routines (16 October 2026, batch h)

Batch h adds 18 routines. Each has five seed proofs with every instruction
reached, 803 mutations caught in all, and an exact boot proof (26 checkpoints)
plus an exact real route that calls it. The harness gained `POPF_STOPS` for
routines whose named code ends with a POPF that restores the caller's IF, and a
`LIVE_FRAMES` row for START `0B49`. Evidence
`D:/f117-gate/p2-batch-h-accept`.

- VGAME `ED10` call software interrupt n with register structures (int86x): 64 instructions, 39 mutations, `full_cycle` 4 calls (`p2-vgame-ed10`).
- PLAYER `026B:0272` calibrate the clock hook timer from the refresh: 54 instructions, 48 mutations, `full_cycle` 2 calls (`p2-player-026b-0272`).
- START `20D6` draw the orders folder pages and wait on each: 449 instructions, 74 mutations, `career_serge` 1 call (`p2-start-20d6`).
- END `4CBC` set the timer rate from the refresh: 54 instructions, 52 mutations, `full_cycle` 5 calls (`p2-end-4cbc`).
- END `1901` show a picture and its palette, run the optional callback, fade in: 64 instructions, 36 mutations, `full_cycle` 1 call (`p2-end-1901`).
- START `38AC` wait for an event on a button table: 58 instructions, 45 mutations, `full_cycle` 9 calls (`p2-start-38ac`).
- VGAME `4973` a mission's seed, opening display, frame counter, clock and palette: 46 instructions, 37 mutations, `full_cycle` 1 call (`p2-vgame-4973`).
- START `8DD8` set the timer rate from the refresh: 54 instructions, 52 mutations, `full_cycle` 17 calls (`p2-start-8dd8`).
- VGAME `0235` run a flight between the clock hook and unhook: 45 instructions, 44 mutations, `full_cycle` 1 call (`p2-vgame-0235`).
- START `141D` copy the generated map, draw sites, route, view overlay and sprite, show the briefing page: 66 instructions, 34 mutations, `full_cycle` 2 calls (`p2-start-141d`).
- START `0B49` arms page hover change: relight the hovered item, restore the one left: 59 instructions, 47 mutations, `full_cycle` 1,425 calls (`p2-start-0b49`).
- END `3CD0` allocate page memory, make the five pages: 47 instructions, 43 mutations, `full_cycle` 1 call (`p2-end-3cd0`).
- END `4722` unpack a compressed picture onto a page, row by row: 38 instructions, 30 mutations, `full_cycle` 2 calls (`p2-end-4722`).
- START `76D1` shut the drivers and hooks down and exit with the code: 43 instructions, 45 mutations, `full_cycle` 1 call (`p2-start-76d1`).
- START `7312` front-end start-up: shared block, shape sets, pages, art, timer, joystick, mouse, random seed: 42 instructions, 36 mutations, `full_cycle` 2 calls (`p2-start-7312`).
- VGAME `EC12` call software interrupt n with a register structure (int86): 52 instructions, 42 mutations, `boot_to_flight` 1 call (`p2-vgame-ec12`).
- PLAYER `13CE` call software interrupt n with register structures (int86): 52 instructions, 43 mutations, `full_cycle` 2 calls (`p2-player-13ce`).
- PLAYER `07EA` show one picture of the intro stream and return the next one's address: 117 instructions, 56 mutations, `full_cycle` 1,592 calls (`p2-player-07ea`).

### 21 routines (16 October 2026, batch g)

Batch g adds 21 routines. Each has five seed proofs with every instruction
reached, 1,017 mutations caught in all, and an exact boot proof (26 checkpoints)
plus an exact real route that calls it. Several are interior entries
(arms of a switch, a loop's continue point, the second half of a routine
another matched body hands back to); the harness gained `LIVE_FRAMES` for
entries whose function frame is already built. Evidence
`D:/f117-gate/p2-batch-g-accept`.

- VGAME `00F0` flight start-up: mission, models, mode, sound file, palette, cockpit pages and HUD recolour (tail 01C1): 105 instructions, 54 mutations, `full_cycle` 1 call (`p2-vgame-00f0`).
- VGAME `01C1` recolour three HUD bitmap rectangles for the current pilot mode: 37 instructions, 34 mutations, `full_cycle` 1 call (`p2-vgame-01c1`).
- START `5BF0` world options: theatre, tension, mission, roster save and exit wipe: 251 instructions, 64 mutations, `central_europe_airair` 1 call (`p2-start-5bf0`).
- VGAME `825D` put a multi-function display up: 84 instructions, 38 mutations, `strike_training_d8` 1 call (`p2-vgame-825d`).
- START `2696` objective line for a photograph or destroy target: 83 instructions, 44 mutations, `full_cycle` 4 calls (`p2-start-2696`).
- VGAME `7663` release the selected store: 218 instructions, 68 mutations, `korea_strike` 2 calls (`p2-vgame-7663g`).
- START `A015` the formatter's integer conversion: 98 instructions, 40 mutations, `full_cycle` 8 calls (`p2-start-a015`).
- VGAME `C9AE` 8 key bay doors, then the key dispatcher tail: map pan, nav cursor, release and lamps: 343 instructions, 52 mutations, `korea_strike` 1 call (`p2-vgame-c9ae`).
- START `9EB3` the formatter's string field: 66 instructions, 42 mutations, `full_cycle` 81 calls (`p2-start-9eb3`).
- END `5825` the formatter's string length: 11 instructions, 28 mutations, `full_cycle` 7 calls (`p2-end-5825`).
- END `3887` show the landed screen: picture, three panels, four animation frames, prompt: 122 instructions, 45 mutations, `airair_type6` 1 call (`p2-end-3887`).
- END `099D` place and draw a debriefing map icon: 97 instructions, 41 mutations, `full_cycle` 1 call (`p2-end-099d`).
- VGAME `D9FE` measure an object from the eye, cull it and defer it for drawing: 114 instructions, 74 mutations, `airair_type6` 848 calls (`p2-vgame-d9fe`).
- START `644A` roster hover: detail strip, colours, record sheet: 69 instructions, 47 mutations, `full_cycle` 2 calls (`p2-start-644a`).
- END `372F` landing pictures and palette fade rates: 145 instructions, 37 mutations, `landing` 1 call (`p2-end-372f`).
- END `5987` the formatter's integer conversion: 98 instructions, 40 mutations, `full_cycle` 2 calls (`p2-end-5987`).
- END `34EA` debrief score: next log record, or scale and return the total: 170 instructions, 53 mutations, `full_cycle` 5 calls (`p2-end-34ea`).
- END `2A01` announce a promotion, a held-back one or a plain debrief: 80 instructions, 44 mutations, `career_promotion` 1 call (`p2-end-2a01`).
- VGAME `4657` place a released countermeasure in a slot and announce it: 83 instructions, 40 mutations, `korea_strike` 2 calls (`p2-vgame-4657`).
- PLAYER `0D0C` palette to its copy and the DAC (black during a fade): 46 instructions, 33 mutations, `full_cycle` 7 calls (`p2-player-0d0c`).
- END `0A98` format and draw a replay event caption: 282 instructions, 99 mutations, `full_cycle` 1 call (`p2-end-0a98`).

### Front-end menu and intro controls (16 October 2026, batch f)

Batch f adds two routines / 331 census bytes. Both have five seed proofs,
full instruction coverage, 80 mutations caught and exact boot (26 checkpoints)
and full-cycle (66 checkpoints) proofs. Evidence `D:/f117-gate/p2-batch-f-accept`.

- START `020F` front-end menu loop: dispatches world, pilot, mission,
  briefing and launch choices, retaining the original event and redraw logic.
  67 instructions, 41 mutations, two full-cycle calls (`p2-start-020f`).
- PLAYER `0696` intro control poll: pending key dispatch, joystick/mouse
  buttons and keyboard handling with the original busy flag around handlers.
  61 instructions, 39 mutations, 2,310,066 full-cycle calls
  (`p2-player-0696`). The 161-byte watch span includes the RET; census credit
  remains 160 bytes.

### Runtime, intro and front end (16 October 2026, batch e)

Batch e adds eleven routines / 2,062 census bytes. Each has five seed proofs,
full instruction coverage, detected mutations (476 total), and exact boot
(26 checkpoints) and full-cycle (66 checkpoints) proofs. Evidence:
`D:/f117-gate/p2-batch-e-accept` and the private directories named below.

- VGAME `450A` key dispatch and radar signature: 74 instructions, 40
  mutations, 275 full-cycle calls (`p2-vgame-450a`).
- PLAYER `0738` timer-driven picture, fade and stream-position step: 53
  instructions, 42 mutations, 7,355 calls (`p2-player-0738`).
- START `038C` launch prompt and animation: 88 instructions, 45 mutations,
  one call; `3CEA` modal dialog and backdrop restore: 83 instructions, 40
  mutations, one call; `7272` roster save: 69 instructions, 46 mutations,
  three calls (`p2-start-{038c,3cea,7272}`). D11 remains a DOS-layer option.
- START `A31A` file-handle write with text translation: 77 instructions,
  53 mutations, six calls (`p2-start-a31a`).
- MPS_LOGO `00DA:0352` sound-overlay loader: 98 instructions, 66 mutations,
  one call; `0146:0BCC` stream refill: 65 instructions, 46 mutations, 25
  calls (`p2-logo-{00da-0352,0bcc}`).
- C runtime initialization: START `8FC6` / END `4EBC` reuse `sm4_cinit`
  (84 instructions, 26 mutations each, two / one calls); MPS_LOGO
  `0146:010E` is the far-code variant (82 instructions, 46 mutations, one
  call). The harness recognizes the three fatal-hook jump targets as the
  end of the named region; original code still executes the fatal exit.
  The overlay loader's DOS-exit arm has the same scoped completion rule.
  Proofs `p2-start-8fc6`, `p2-end-4ebc`, `p2-logo-010e`.

### Matched: VGAME flight, weapons and world

- `3ADA` frame update (the per-frame world update; 848 instructions, all 71
  calls kept): map cell and clamp, first-frame set-up, markers, timers,
  landing field and traffic, tower, field contact, crashes, carrier deck,
  frame counter and frame-rate measure, standing-target count. Replay 8,462
  calls. Declines while D1 or D1REAL is on. `D:/f117-gate/frame-3ada`.
- `1EB0` flight input (the player's flight step; 1,274 instructions, all 121
  calls): keys, stick scaling, holds, trim, recovery, ejection, engine,
  attitude, position, touchdown and crash tests, 16-frame pose history.
  Replay 8,464 calls. `D:/f117-gate/flight-input`.
- `5852` aircraft traffic (998 instructions): formation, pursuit, patrol,
  landing, smoke, markers, staggered firing; divide-error states at four
  sites. Replay 8,464 calls. `D:/f117-gate/frame-5852`.
- `683E` weapons in flight (886 instructions): target selection, seekers,
  steering, gravity, terrain contact, blast, supply objectives, proximity
  damage, markers; four seeds, all 25 callee-return checks. Replay 8,459
  calls. The supply arm (normally bypassed by D5) is reached only by a staged
  fixture; the entry keeps the D5, D8, D1TTL, D1SLOT, D1PROX and D1ACCEL
  hooks. `D:/f117-gate/frame-683e`.
- `5046` ground defences (195 instructions): damage/stealth bars, site reload
  and engagement, incoming marker and arc, warning countdown; the reload's
  staged divide faults hand back at `5122`. Replay 8,465 calls.
  `D:/f117-gate/nav5046`.
- `63F2` `vgame_traffic_lock_and_fire` and `528D` `vgame_ground_site_engage`
  (`full_cycle` 9 and 12 calls), `0D14` `vgame_nearest_object_lookup` (thin;
  declines while D34 is on). `D:/f117-gate/p2-batch3`, `p2-batch4`.
- `066F8` aircraft activation and `072D8` destruction (destroy reaches the
  sound gate's uninitialised driver thunk `0xD414`, so the harness needs an
  entry for it).
- `073C8` `vgame_target_damage` (target scoring): its compound branch
  conditions make most random states incomparable, so the routes are the
  stronger check; planted weapon-lock values in `tests/func_lockstep.c`.
- `04777` projectile motion and gunfire refill, `01007` terrain collision
  probe, `0D3F9` sound priority (engine reassertion kept even when a request
  is rejected), `0D87C` scene finish.
- `3A4E` `vgame_pause`: a real Alt+P / repeated-Alt+P / Space / quit probe
  matches 54 checkpoints (`matched_pause_wait`, route `pause_resume`).
- `1E79` timer tick: camera replay 53,226 calls.
- `01C0E` `vgame_isrclock_init` (timer vector hook) and `01E72`
  `vgame_frame_mission_start` (BIOS clock sample): routes only.
- `0F8EC` `vgame_free` and `0F91A` `vgame_malloc`.
- `0BAA2` `vgame_init_models` (thin at random states; the gate's routes run
  it).
- `09A0` `vgame_load_table_pair` (thin), `F5F2` `vgame_open_file_handle` and
  `F458` `vgame_read_file_handle` (text-mode read; its CR-LF path needs
  scripted DOS answers, `g_dos_script[].put`; `full_cycle` 11 calls).
  `D:/f117-gate/p2-batch5`, `p2-batch6`, `p2-batch7`.
- `0FAE:0002` segment entry binding: all 27 instructions by authored
  fixtures.

### Matched: VGAME scene and rendering

- `114A:0070` line clipped to an explicit box, optionally on both pages
  (16 Oct, batch d): 78 instructions, 38 mutations, `full_cycle` 4,997
  calls. Its far callee `0FB2:0324` is the far entry of the raster line
  clipper, now named `vgame_clip_raster_line_far` (it was mislabelled "sky
  bands"). Proof `D:/f117-gate/p2-vgame-114a-0070`.

- `D8AF` viewport bounds, projection centre and raster callbacks (16 Oct):
  all 85 instructions at five seeds, 45 mutations caught; `full_cycle`
  275 calls. Proof `D:/f117-gate/p2-vgame-d8af`.

- `0B05` tile-object loading, `120A:0CE9` model fill command stream,
  `8462` map redraw and `03CE` scene dispatch (16 Oct): five seeds each,
  42/41/52/44 mutations; `full_cycle` 1/32,334/1/275 calls. The command
  stream's shared condition tail and RET below its entry are included in
  the harness's code watch and instruction coverage.
  Proofs `D:/f117-gate/p2-vgame-{0b05,120a-0ce9,8462,03ce}`.

- `2F9D` view composition (938 instructions, all 66 calls): view modes,
  camera, redraw, scene call, overlays, readouts. Replay 8,456 calls.
  `D:/f117-gate/frame-2f9d`.
- `BCEF` camera-image pass (all 49 calls kept, 1,863 census bytes; replay
  7,880 calls) and `983E` moving-object pass (all 38 calls, 1,724 bytes; F3
  replay 550 calls). `D:/f117-gate/camimage`, `moving-objects`.
- `04E6` `vgame_scene_walk` (terrain tile rings and their objects),
  `DC46` `vgame_compose_walk` and `1450` `vgame_horizon_bands` (which calls
  `observe_game_draw` at entry; defects below): `full_cycle` 275-276 calls
  each. `D:/f117-gate/p2-scene`, `p2-compose`, `p2-pair`.
- `BB0D` `vgame_draw_model_in_view`. `D:/f117-gate/p2-batch5`, `p2-batch6`.
- `130D:033F` `vgame_model_edge_clip` (15 Oct; 206 instructions, five seeds,
  64 mutations, `full_cycle` 184,777 calls). Its near-`RET` accept path at
  `031B` (a latent quirk of the original) is handed back to the original
  code, so that path is not independently tested. `D:/f117-gate/p2-vgame-033f`.
- `0FB2:0330` `vgame_clip_raster_line` (16 Oct; 149 instructions, five
  seeds, 60 mutations, `full_cycle` 10,688 calls): clips a raster line
  against the view rectangle and draws the visible part. Its temporary
  divide-error vector is saved and restored exactly; the handler `04DD`
  stays original code (no route reaches it). The lockstep harness keeps its fixed
  clock limit across that handler's IRET and treats any return to the
  caller inside a nested run as an escape, both scoped to this entry.
  `D:/f117-gate/p2-vgame-fe50`.
- `1974`/`19FD` camera sky/ground strips (including sixteen-band walks;
  camera-gradient replay 7,851 / 1,108 calls; that route checks rendering
  and makes no mission-completion claim).
- `C45C` camera darkening (77x61 inset): reconnaissance with F6 calls it
  1,608 times, all 150 checkpoints (`recon_darken`, `matched_camera_darken`).
- `0FB2:01E6` closed polygon fill (382,365 replay calls,
  `matched_polygon_fill`), `0FB2:06A6` clipped span edge (1,529,189 calls) and
  the span-emitter entries `0FB2:05A4`/`05B3` (inside the already-counted
  `0555` region, so no byte credit).
- `0FB2:084E` span-table divide-error (INT 0) handler, matched as an
  interrupt entry ending in IRET; the lockstep plants the interrupt frame.
  It runs in 28 of the 39 routes gated at the time; a sign-flipped handler
  changes `vietnam_airair`.
- `vgame_outcode` (image `0x0FFDC`), registered at its real `0FB2:04BC`
  entry (dispatch checks CS); the strike route runs it 4,361,362 times.
- `11A8` moving-map downward frame and MPS_LOGO `006D:0678` sprite run
  (including opaque zero-count runs of 256 writes); `full_cycle` reaches
  both.
- Low-detail sky entry (with the batches of 8-9 Oct below).

### Matched: VGAME HUD and cockpit

- `0300` armament panel background (16 Oct, batch d): 71 instructions, 50
  mutations, `full_cycle` 1 call. Proof `D:/f117-gate/p2-vgame-0300`.

- `D578` cockpit element recolouring (16 Oct): all 77 instructions at five
  seeds, 36 mutations caught; `full_cycle` 39 calls. Keeps the original far
  driver calls through the presentation observer. Proof `D:/f117-gate/p2-vgame-d578`.

- `1058:0016` HUD pass (2,488 census bytes), `79BE` cockpit panel frame (2,090
  bytes): F3 replay 550 calls each. `8A90` navigation grid: F3 replay 314
  calls (the `navigation_grid` route). `9515` objectives panel: the F10
  `objectives_panel` route calls it four times.
- `17B4` `vgame_attitude_indicator_draw`: `full_cycle` 275 calls.
  `D:/f117-gate/p2-batch4`.
- `1058:09D8` HUD colour setup and `1058:0A2C` small/full layout: real C/C
  toggles match 54 checkpoints (`matched_hud_layouts`, route
  `hud_layout_toggle`).
- Projected HUD diamond (`B0C2`): the laser-guided training route calls it
  1,051 times.
- `0B171` weapon-lock marker, `8A18` right-aligned number, `881F` HUD line,
  `114A:0280` display fill, `796E` cockpit initialiser, `114A:03AD`
  navigation symbol, `114A:03FD` sprite rectangle, `114A:014F` readout
  outline, `114A:0006` active-block line and `114A:01DB` target box (checked
  with returning driver thunks; the routes use the real driver).
- `0D6DD` cockpit canopy (side frames, posts, landing-approach cue).
- `114A:02CE` `vgame_panel_navigation`: routes only (two call sites run every
  frame of normal flight).

### Matched: START

- `A230` text-mode file read (the copy of VGAME `F458`, all 101 original
  instructions checked against it), `73CE` graphics pages set-up, `6D07`
  roster settle and save and `615C` roster page (16 Oct, batch d): 94/79/65/98
  instructions, 60/58/46/56 mutations, `full_cycle` 38/2/2/1 calls. The
  read's shared DOS return `98BD-98C9` earns no duplicate coverage credit.
  Proofs `D:/f117-gate/p2-start-{a230,73ce,6d07,615c}`.

- `0448` launch animation, `3F33` key/button wait, `1308` mission-order
  decoding/briefing and `30A0` palette fade (16 Oct): five seeds each,
  47/38/53/67 mutations; `full_cycle` 1/542/2/15 calls. The four palette
  copies in `1308` use a helper kept out of MSVC's inline expansion: the
  same MOVSW/MOVSB order and clocks, without the optimizer stall.
  `30A0`'s rare IDIV fault returns to original code (not fixture-tested).
  Proofs `D:/f117-gate/p2-start-{0448,3f33,1308,30a0}`.

- `4239` `start_mission_generate` (1,332 instructions once its nine-entry
  switch is resolved; all 339 room claims and 60 calls audited; one
  otherwise-unreachable timing arm staged). The boot route runs it once.
  `D:/f117-gate/start-4239`.
- `40F0` `start_mission_take_out_sites` (theatre load, mission generation,
  random destroyed sites), `7904` `start_choose_world_record`, `0FA3`
  `start_brief_key_loop`, `149C` flight-plan display, `6DB8` pilot options
  dialog, `07EE` armament screen: `full_cycle` one or two calls each.
  `D:/f117-gate/p2-batch7`, `p2-pair`, `p2-start-0fa3`, `flight-plan`,
  `pilot-options`.
- `0963` `start_arms_screen_loop` (thin), `A59A` `start_sopen_file` (its create
  path needs scripted INT 21h answers), `008D` `start_draw_pilot_card`,
  `64E5` `start_draw_pilot_record_sheet` (thin; its span holds six interior census
  entries), `74B1` `start_world_page_objects`, `623E`
  `start_roster_event_loop` (thin) and `6861` `start_draw_pilot_row`.
  `D:/f117-gate/p2-batch3` to `p2-batch6`.
- `39F7` (with END `1CF5`) menu input and pointer update: `full_cycle` 7,015 /
  487 calls. `D:/f117-gate/p2-next`.
- `3641` screen loader with optional overlay callback: `full_cycle` 5 calls.
- `9D67` (with END `56D9`) formatter conversion-state initialiser:
  `full_cycle` 89 / 9 calls. `D:/f117-gate/format-init`.

### Matched: END

- `04D2` mission-route marks and `3D4F` debrief graphics-region preparation
  (16 Oct): all 94/103 instructions at five seeds, 42/35 mutations caught;
  `full_cycle` one call each. Proofs `D:/f117-gate/p2-end-{04d2,3d4f}`.
  This four-routine batch also shares one normal /O2 matched object between
  the two runtime archives. Full gate, all 45 N=1 routes and boot N=4/9 pass;
  evidence `D:/f117-gate/p2-batch-c-accept/final-gate-verdict.json`.

- `02A4` debrief opening and `1607` palette fade (16 Oct): five seeds,
  46 mutations each, `full_cycle` 1 and 4 calls. The fade's rare IDIV fault
  returns to original code (not fixture-tested).
  Proofs `D:/f117-gate/p2-end-{02a4,1607}`.

- `2BEC` debrief narrative builder (444 instructions). END `206A`, once
  labelled a fatal-error handler, is the "Press Selector to continue" prompt.
  `D:/f117-gate/debrief-narrative`.
- `0714` `end_replay_step` and `2092` `end_wait_answer`: `full_cycle` 1 and 17
  calls. `D:/f117-gate/p2-batch7`.
- `0100D` `end_ribbons_draw` (thin at random states; called from nine sites).
- `1CF5` and `56D9`: with START above.

### Matched: SETUP, PLAYER, MPS_LOGO, DSWAP and the runtime

- MPS_LOGO's far-model C runtime: `0146:1510` text-mode read (a reviewed
  copy of START `A230`: far return, arguments two bytes higher), `02BA`
  `fclose`, `03B6` `fread`, `0D72` `_openfile` and `0045:0116` `fread` into a
  far buffer through a 512-byte bounce buffer; with `006D:05BE` RLE sprite
  frame and DSWAP `1520` `_openfile` (16 Oct, batch d). 62/44/43/58/46/42/54
  mutations; `full_cycle` 25/1/561/1/416/82/4 calls. `05BE`'s 288-byte watch
  span encloses the matched helper `0678`, so its proof covers 122
  instructions (112 its own); census credit stays its 186-byte row. `03B6`'s
  two divide faults return to original code (not fixture-tested). The far
  `fread` calls `RETF 8` helpers through a local far-call trap that raises
  the return SP by the pop count. Proofs `D:/f117-gate/p2-logo-{1510,02ba,
  03b6,0d72,0045-0116,006d-05be}`, `p2-dswap-1520`. Batch: 13 routines, 657
  mutations, 26 boot and 66 full-cycle checkpoints exact each, 65 optimized
  seed runs; full gate and all-route N=1 presentation:
  `D:/f117-gate/p2-batch-d-accept`. Census gain 13 functions / 2,809 bytes.

- PLAYER `0BE2` LZ91 block decoding and `03FE` stream-buffer room/wrap,
  SETUP `0A9F` overlay loading (16 Oct): five seeds each, 47/43/45
  mutations; `full_cycle` 34/823,304/1 calls. SETUP's terminal DOS error
  arm counts as completing the named code at its original INT 21h/4Ch.
  Proofs `D:/f117-gate/p2-player-{0be2,03fe}`, `p2-setup-0a9f`.
  This batch's 13 routines passed 26 boot and 66 full-cycle checkpoints
  each, 611 mutations and 65 optimized seed runs. Full gate and all-route
  N=1 presentation evidence: `D:/f117-gate/p2-batch-b2-accept`.
  Census gain: 13 functions / 3,674 bytes.

- SETUP `0010` `setup_questions` (505 instructions): eight real startup cases
  match 320 checkpoints (all four sound choices, calibration, keypad quit, D3
  on and off); four startup regression routes. `D:/f117-gate/setup-0010`.
- MPS_LOGO `006D:0112` `logo_sprite_set_load` (434 instructions): the boot
  route matches all 100 checkpoints of its 10M-clock prefix.
  `D:/f117-gate/logo-07e2`. MPS_LOGO `006D:0002` encoded-sprite recolouring:
  all 63 instructions by authored fixtures. `D:/f117-gate/entry-palette`.
- PLAYER `0244` `player_stream_picture_file`: `full_cycle` 7 calls.
  `D:/f117-gate/p2-batch4`.
- PLAYER `1602` `player_crt_setargv` (15 Oct; the C start-up's argument
  vector, 204 instructions; it leaves by `JMP [1ABC]` with the stack lowered,
  so the lockstep accepts that exit for this routine only): five seeds, 61
  mutations, `full_cycle` 2 calls. `D:/f117-gate/p2-player-1602`.
- SETUP `19B6` `setup_crt_setargv` (204 instructions; the same startup as
  PLAYER `1602`, with its own `JMP [0EBA]` exit): five seeds, 61 mutations,
  `full_cycle` 1 call. MPS_LOGO `0146:1894` `mps_logo_open_file` (163
  instructions; the far-return form of START's runtime open): five seeds,
  59 mutations, `full_cycle` 1 call. Each keeps all 66 checkpoints and the
  final hash. Census credit: 2 functions / 817 bytes, measured against the
  same census before and after; shared tails add no credit.
  `D:/f117-gate/p2-setup-19b6`, `p2-logo-1894`; batch gate and N=1
  presentation evidence: `D:/f117-gate/p2-setup-logo-accept`.
- MPS_LOGO `0006:0072` `mps_logo_play_intro` (248 instructions, all 26 far
  calls retained): music selection, palette/sprite load, timed frames and
  keyboard cancellation. Its runtime allocator returns with SP lowered by
  `0318h`; the local return trap and the harness's escaped-call check both
  account for it. Five seeds, 58 mutations including the wrong-trap negative
  control, `ibm_boot` 26 and `full_cycle` 66 exact checkpoints, 1 call each.
  The watch envelope is 712 bytes including gaps; census credit stays 707.
  MPS_LOGO `0146:08CC` `mps_logo_crt_setargv` (205 instructions) is the
  far-model startup argv builder: five seeds, 62 mutations, the same two
  exact routes and positive counts. Its indirect far return keeps argv on
  the stack; the harness accepts that terminal for this entry.
  Proofs `D:/f117-gate/p2-logo-0072`, `p2-logo-08cc`; batch evidence
  `D:/f117-gate/p2-logo-main-argv-accept`. Census gain: 2 functions / 1,109 bytes.
- DSWAP `0010` `dswap_load_from_drives` (197 instructions), `117E`
  `dswap_crt_setargv` (204) and `1D66` `dswap_open_file` (163): five seeds
  each, 56/61/60 mutations caught, 26 boot and 66 full-cycle checkpoints
  exact, one boot call and four full-cycle calls each. The open routine's
  427-byte watch envelope includes eight gap bytes; census credit is 419.
  The loader exposed a runtime reporting bug when its nested call exits
  the program: the dispatcher now retains its override index across the
  module replacement, with a ROM-free regression and an old-runtime
  negative control. Proofs `D:/f117-gate/p2-dswap-{0010,117e,1d66}`;
  batch evidence `D:/f117-gate/p2-dswap-accept`. Census gain: 3 functions /
  1,338 bytes. The near argv terminal retains its stack allocation.
- PLAYER `005C` `player_prepare_playback` (120 instructions; memory,
  timer services and video mode before playback) and VGAME `E586`
  `vgame_load_file_buffer` (119; opens a file, allocates a paragraph-rounded
  buffer and reads it in FFFFh-byte chunks): five seeds each, 49 and 48
  mutations, 26 boot and 66 full-cycle checkpoints exact, `full_cycle` 2 and
  4 calls. Proofs `D:/f117-gate/p2-player-005c`, `p2-vgame-e586`; batch
  evidence (with `0FB2:0330` above) `D:/f117-gate/p2-batch-fe50-accept`.
  Census gain for the three: 3 functions / 1,021 bytes.
- Twenty-two support routines: PLAYER's picture open/refill and linear copy,
  the four timer shutdowns, five mouse wrappers and SETUP's mouse probe,
  file-name comparison, BIOS text writer, register-block interrupt call and
  memory free, the logo and debrief record-table fills, logo file
  loading/freeing and START heap resizing; `full_cycle`, `ibm_boot` and
  `setup_calibration` together call every one. `D:/f117-gate/leaf-batch`.
- DSWAP's checked allocator and five Ctrl+Break restore helpers (including
  allocation-error results: this checks the caller when cleanup returns, not
  real out-of-memory termination); five Ctrl+Break installers (VGAME, START,
  END, DSWAP, SETUP) and DSWAP's DOS allocate/free wrappers.
- The batches of 8-9 Oct: 37 + 34 + 47 VGAME, 34 + 40 + 45 START/END and 21 +
  44 + 64 small-program routines, including the picture decoder's RLE row and
  LZW step in VGAME and END (the step is routes only), the front ends'
  retrace re-sync, PLAYER's two tick-rate setters and VGAME's low-detail sky
  entry. The lockstep includes VGAME `1377:00F3`'s loop and RET below its
  entry.

### Defects found in matched code and the harness

Each is fixed; the named regressions keep the failing seed or case.

- **Arguments read late.** `13B9` `vgame_map_project` and `04ABA`
  `vgame_eventlog_add` cached stack arguments the original reads after its own
  stores, which can alias them when DS and SS overlap
  (`matched_map_project_stack_alias`); `13B9` also read `[950E]` before its
  push of BP, which can wrap onto it (`matched_map_project_saved_bp_alias`).
  VGAME `4B8F` (objective place) must read its target after clearing the
  output buffer. The in-place 32-bit shift's stores go through live BX after
  the original helper returns, even when the stack overlaps that helper's
  code (`matched_shift_stack_alias`).
- **Byte and word tests.** `or ax, ax` after a call was a byte test in `BAA2`
  and twice in `73C8` (wrong SF; seed `0xfaf385733f77`, `matched_or_ax_word`).
  `73C8` also stopped 12 instructions short in its slot walk.
- **Wrapping and relocation.** The menu hit test (START `393D`, END `1C3B`)
  assumed eight contiguous bytes for a list at FFFFh (`matched_menu_hit_wrap`).
  A fixed segment in the record-table fills and a constant zero in the five
  Ctrl+Break installers passed zero-base fixtures but diverged after DOS
  relocated the program; fixtures now vary the relocated operand.
- **Code written during a run.** Fuzzed pointer writes from `683E` could
  overwrite a planted callee stub; such states are skipped
  (`matched_projectile_stub_overwrite`). Fuzz inputs that write code are
  watched per instruction (`matched_order_code_alias`,
  `matched_frame_code_alias`). START/END's shadow-text helper keeps PUSH SS's
  memory write when it aliases the stack (`matched_shadow_stack_alias`).
- **Fix sites inside matched bodies.** A named body ran through D1's site
  (`0x441D`, in `3ADA`), so the fix never ran in the recompiled engine. Five
  matched routines contain a fix site after their entry (`3ADA` with D1 and
  D1REAL, flight input with D1REAL, `vgame_scene_set_override` with D34,
  `vgame_detect_evaluate` with D6); each declines while that fix is on. No
  route triggers D34 or D6 yet.
- **Shared hooks.** `1450`'s address is also the presentation observer's
  frame-phase hook; it calls `observe_game_draw` itself and is on
  `code_overrides`' deliberate list.
- **Room and returns.** Four divides in `3ADA` sat outside their room claims;
  `room_audit.py` (private) checks every claim. A missed `RET 8` let the
  original finish every state unseen; the harness now fails any callee return
  the named code misses. `149C` had a stray resume point only a non-default
  seed saw, so every body is tested at several seeds. `0D14`'s fixture reached
  its "none found" exit only by chance (189 of 190 at seed `0xd1699b13b019`);
  one state in sixteen now has every cell list empty. Nested interrupt runs
  stop at the caller's next event through `support_int`.
- **Changed callee return (16 Oct).** VGAME `683E`'s fuzz state changed
  `C830` from RET to RET n after earlier normal returns at the same site.
  The harness's escape test used that earlier stack depth as evidence for
  the later call. Exact original changed-return states now distinguish
  original completion from an escaped trap, without skipping comparison or
  granting named coverage. Regression `matched_projectile_changed_return`;
  wrong-trap, clock and movement negative controls all fail. Reproduced on
  unchanged master before fixing the harness.
- **SETUP event-limit alert (14 Oct).** Not a game-code defect: two atomic
  native service clocks were counted as guest instructions; the ledger now
  tells them apart (`native_service_boundary`;
  [architecture.md](architecture.md#time)).

## Phase 3 - fixes and enhancements (switchable)

- [x] **Code overrides** attach fixes: hand-written C registered for a module
      address, pinned to the shipped file by hash. Normal play enables all
      fifteen; core/headless and ordinary parity keep them off. `--fix none`
      restores the original and `--fix ID` adds selected fixes. Inert when off. Details:
      [architecture.md](architecture.md#the-run-time).
- [x] **Fixes for the original's bugs** ([bugs.md](bugs.md)), each on a switch:
      D1 (a frame limiter at VGAME 0x441D holds a fast machine to GOG's 11.6
      frames a second; `tools/d1_check.py`, route `d1_fast_machine`), D2
      (speech played from the machine's schedule instead of a busy-wait), D3
      (keypad digits with NumLock off answer SETUP's sound question instead of
      quitting to DOS), D4 (secret airstrips), D5 (supply drops earn credit;
      `cargo_d5_fixed`), D6 (a detection cover of 0 reads as 4; optional, the
      cells are not proven a defect), D8 (the laser-guided bomb's pitch clamp;
      it also fixes D7), D11 (saves written to a temporary file and renamed),
      D12 (END's signed rating), D34 (destroyed-object table), and the
      high-speed combat fixes D1TTL, D1SLOT, D1PROX and D1ACCEL
      (`tools/d1ttl_check.py`, `d1slot_check.py`, `d1physics_check.py`) and
      the optional D1REAL (below). D10, D35 and D36 are closed as not
      established for DOS or not reproduced
      ([legacy-reports.md](legacy-reports.md)). No speculative fix is offered;
      D96 is the Reimp's own UI wait, not a patch for this START. A session
      recorded with fixes on names them in its log and replays with them.
  - *Speed* ([speed-sweep.md](speed-sweep.md)): the app runs at 20 million
    instructions a second with no limiter
    ([why](speed-sweep.md#fast-machine-default-9-october-2026)): the game's
    own clamp holds S at 15, no swings in 214 flights. D1's 11.6 fps cap gives
    a 1.2882x mission clock rather than real time
    ([D1 follow-up](speed-sweep.md#d1-follow-up-9-october-2026)), and a
    [15 fps cap](speed-sweep.md#actual-15-fps-limiter-experiment-9-october-2026)
    gives 1.25x, so no cap restores real-time pacing.
  - *Combat at high S*
    ([measurements](speed-sweep.md#controlled-threat-profile-combat-measurement-9-10-oct-2026)):
    the game's signed hit guard suppresses SA-5 damage at S >= 14 (D1TTL);
    a seeker-cleared lifetime can occupy the launcher's slot (D1SLOT); the
    proximity band and acceleration depend on S (D1PROX, D1ACCEL). All 21
    staged physics cases pass under both engines. Bursts at 20 MIPS remain
    slightly fewer than at 9 (pooled 36 pairs -0.67 a flight, 95% -1.25 to
    -0.08).
  - *D1REAL*
    ([evidence](speed-sweep.md#optional-real-time-cadence-10-oct-controlled-airborne-checks-complete)):
    an optional real-time cadence; seven clock/dialog cases pass at 9, 20 and
    40 MIPS in both engines and airborne clocks run at real time. The
    original's key-poll timing still limits exact whole-flight determinism
    across speeds, so it does not establish a safe speed.
    The 17 Oct private cadence comparison keeps eight as the default:
    14 passes the clock/dialog cases at 20 MIPS, 15 fails exact 2x
    compression, and both higher rates fall behind at 9 MIPS. Input response
    improves, but flight paths change; neither rate establishes developer intent.
- [ ] **60+ fps and 4K presentation.** Optional and off by default
      (`--present replay|interp|rerun`, `--present-scale N`, `present =` in
      f117a.ini). Design, measurements and history:
      [presentation.md](presentation.md). Rules: the original's logic tick and
      control timing are unchanged; its bitmap text, sprites and artwork are
      kept and integer-scaled; N=1 stays the exact original; an old path is
      retired only when its replacement passes N=1 equality. Target: 4K at
      60 Hz, and any scale and refresh rate, lower or higher. Higher
      resolution and higher frame rates are experimental and keep the
      original renderer's quirks (road and coastline jitter, which the
      original shows too; drawing-order mistakes; lines of one width at any
      distance).
  - *Done:* every mode, rerun included, keeps all 45 N=1 routes exact
    (259,240 logic frames, 259,166 in-between pairs, every guest hash); the
    original executable matches 2,619 fill-style and 3,953 graphics-driver
    fixtures. **Sharp mode** (`--present replay --present-scale N`, verified):
    model polygons re-projected from exact camera snapshots, including
    near-clipped polygons and span-rasteriser fills; footprints the
    original's clipper gets wrong accepted at N>1 (worst-frame fallback 84%
    to 1.7%); lines on the fine grid (360 Reimp reference strokes exact); HUD,
    panel, map arrow, world outlines, dot markers and sky/ground bands drawn
    from game state (29,594 HUD calls, 11,490 panel calls and 714 map blits
    match the original). **In-between frames** (`--present rerun`, integrated
    and gated 15 Oct): the original's unmodified draw re-run on a throwaway
    copy of the machine with camera and object inputs nudged, refined at any
    scale, timed by a smoothed playback clock (`F117_PRESENT_CLOCK=raw` for
    the old one); 1,638,579 copy checks leave real memory, machine and fix
    state unchanged, and unnudged copies reproduce 259,787 frames exactly.
    The earlier pairing interpolation (`--present interp`) remains; identity
    pairing is off (it deformed objects; `F117_PRESENT_IDENT=1`). **Threaded
    presentation:** fullscreen Direct3D 11 3840x2160 at 59.99 Hz with vsync
    at N=9 passed on 13 Oct (one late interval in 7,715 flight pictures);
    hidden 4K throughput 119-150 fps. A palette frozen in threaded `interp`
    and `rerun` at N>1 (gun rounds solid red, fades frozen) was fixed on 15
    Oct. **Scene-input capture** (`F117R_OBSERVE_SCENE=N`,
    `tools/scene_capture.py`, `tools/scene_model.py`) is identical between
    engines and its decoded topology agrees with the original; the project's
    own scene renderer (`src/present/scene_mesh.c`) is an unconnected draft,
    no longer the main path.
  - *Owner verdict (15 Oct):* rerun's motion at 4K accepted "at least for
    now" (fullscreen, apart from the original's jitter).
  - *Open:* clock tuning against picture age; ask before any visible or
    fullscreen test; the small `DF03`/`C2C8` vertex group that nudges less well;
    fullscreen pacing: the longer 17 Oct air-to-air run fails (17,241 pictures,
    p99 28.70 ms, 3.70% over 25 ms; exact guest state). Strike and landing
    passed the earlier short checks; see presentation.md for evidence;
    hidden profiling locates copied-draw CPU cost (46% main-thread samples
    in linear override dispatch); lookup optimization is not yet implemented;
    line widths by drawing role; weapon and target symbology; long
    cargo/career flights and slower machines. What the owner decided
    against is under "Possible future work" below and is not scored.

## Possible future work (decided against for now, not scored)

Things the owner decided not to do. They are not scored by `tools/progress.py`
(owner, 17 Oct 2026) and stay here as ideas that may be taken up later, as with
Phase 4. The decisions and their reasons are in
[presentation.md](presentation.md) ("Stop improving primitive pairing", "The
jitter is the original's; no smoothing", "Do not extend the draw distance").

- [ ] **Smooth the original's road and coastline jitter**, at N=1 or any higher
      scale (owner, 15 Oct 2026: it is the original's own, shown by the GOG
      DOSBox release too; N=1 stays authentic and the finer modes keep the
      original renderer's quirks).
- [ ] **Refine clipped or off-screen outlines**: 6.1% of outline strokes in the
      owner's sharp flight are still drawn from scaled whole-pixel endpoints.
- [ ] **More work on primitive pairing** for in-between pictures (owner, 15 Oct
      2026: stopped; identity pairing stays opt-in, `F117_PRESENT_IDENT=1`,
      because it deformed objects; in-between pictures come from `rerun`).
- [ ] **A longer draw distance** (owner, 15 Oct 2026).

## Phase 4 - checked by people and independent references

What a machine cannot judge by itself: it needs a person, or a reference that
this project did not build. Not scored by `tools/progress.py` (owner, 15 Oct 2026).

- [ ] **A person playing it:** controls, joystick and mouse, saves, the feel.
      Sessions already record themselves for replay.
- [ ] **Roland through Munt** (libmt32emu): the MT-32 music plays in the game
      and renders offline from a MIDI log; the interpreter's and recompiler's
      MIDI logs are byte-identical and offline renders are repeatable. Live
      output is not a stable PCM reference
      ([architecture.md](architecture.md#audio)). Open: independent reference
      PCM and a listening check.
- [ ] **Refresh rates above 60 Hz:** `rerun` costs about 6 ms a step, so 144 Hz
      needs parallel copies, which need the observer, the recompiled-code
      runtime and the fix state per machine ([presentation.md](presentation.md)).
      Moved here on 17 Oct 2026: the owner has no convenient high-refresh
      display to test on, so it cannot be judged by the people who would use it.
- [ ] **Coastline transitions:** the coastline's topology transitions are an
      issue of the original game itself (the owner, 17 Oct 2026; its jitter
      shows in the GOG DOSBox release too). Not planned for the finer modes; a
      person judges whether it is ever worth changing.

## Housekeeping

- [x] Phase 2 credit by byte (16 Oct): `F117_NAMED_COVER` in the lockstep and
      route captures measure the census bytes named code runs
      ([progress.md](progress.md#what-is-measured)).
- [x] `src/machine/dos.c` split by service (`dos_memory.c`, `dos_programs.c`,
      `dos_files.c`, `dos_keyboard.c`, `dos_video.c`), the mouse driver in
      `mouse.c`; routes agreed before and after at every checkpoint.
- [x] Build-from-scratch steps in the README, tested on a fresh public clone.
- [x] `.gitattributes` for line endings.
- [x] An automated Windows build on GitHub with ROM-free CPU, machine and
      comparator tests and the route milestone tests.
- [x] Docs kept as reference, not diaries: session history lives in git, one
      place per fact ([repeated-processes.md](repeated-processes.md) tracks
      every repeated process).
- [x] Gate workers: 24 were no faster than 16 with identical verdicts, so 16
      stay; unit tests run on eight workers (`--unit-jobs 1` for serial).
      [repeated-processes.md](repeated-processes.md).
