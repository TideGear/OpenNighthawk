# Roadmap

What is done and what is left. The game's own bugs are in [bugs.md](bugs.md);
how parity is built and checked is in [architecture.md](architecture.md); the
commands, current figures and traps are in [../handoff.md](../handoff.md); the
scoreboard is `py tools/progress.py` ([progress.md](progress.md)). Update this
page with every piece of work, and keep it short: one entry per item, the
evidence linked or in the commit, not narrated here.

The project keeps **1:1 parity first**: the same player actions play out as
on the original's hardware. Fixes and enhancements are switchable, with the
original independently testable (Phase 3). Phase 4 holds the checks only
people and outside references can make.

## Phase 1 - recompiled, 1:1 with the original

### Done

- **Translation.** All 17 code files and the LZEXE decompressor: 89,443
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). Changed code and declined override
  entries fall back to the interpreter.
- **Engine parity.** 63 scripted routes (62 sessions) are identical between
  the interpreter and the recompiled code at every 50-million-clock checkpoint
  and at the end; every translated instruction is held to the interpreter from
  random states (5,723,776 comparisons under each of the two timing profiles,
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
  with a staged roster (`tools/career_rank6.py`; not an earned rank-6 career);
  the START quiz option; the original programs' own error handlers on ten damaged
  installs (`tools/route_damage.py`, a private copy per run). Per route: [../tools/routes/README.md](../tools/routes/README.md).
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
  DAC values (the other, p037, is a single sample of the title screen's fade-in,
  listed in `tools/ref86box/expected_misses86.txt`). The same
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
- **The gate** (`tools/build_recomp.py`): translate, build, unit tests,
  coverage, both engines on every route, instruction lockstep under both
  timing profiles, matched-routine lockstep at three seeds, and the
  routes-only and event-limit checks, with every interpreter route verified
  fresh. Each batch's gate time is in the [batch table](#batch-table); costs
  and what was optimised: [repeated-processes.md](repeated-processes.md).
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
    are charged their measured cost (92 of 93 probe blocks exact against MS-DOS
    5.00 with MOUSE.COM 6.26, the other INT 21h AH=2Ch by a run-to-run 4 cycles; 5.00 and 6.22 cost the same on the game's calls,
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

- [x] **First: fix the original's remaining bugs** (owner, 25 Oct 2026: bug
      fixes come before matching). The 31 rows of [bugs.md](bugs.md)'s
      smaller-defects table that had no fix and no recorded reason were re-read
      against the DOS instructions and compared with Mac 2.3.2 and Amiga 3.01
      (verdicts: [bugs.md](bugs.md#smaller-defects-q-series)). Fixed: ten
      defects (Q7, Q11, Q12, Q14-Q20), three with an owner-chosen behaviour
      (Q13a, Q21, Q22) and the hardening group (Q4, Q13b, D18, Q23), merged at
      `95a5730` after a combined gate (`D:/f117-gate/bugfixes-20261025/`;
      [Windows CI passed](https://github.com/TideGear/OpenNighthawk/actions/runs/40252845423));
      then D101 and Q12's marker-size sibling Q12b were measured and fixed,
      passing combined acceptance with batch be
      (`D:/f117-gate/p2-batch-be-accept/combined-final-verdict.json`). That
      makes 34 normal-play switches (the Phase 3 table). Ten code quirks with
      no effect and four rows that were never the original's defects stay
      documented in bugs.md. The fixes add no matching credit.
- [ ] **Name the translated routines and data**, with explanations, drawing on
      the Reimp's mapping; matched functions replace translations one at a
      time, each held to the same parity checks. Method:
      [architecture.md](architecture.md#matched-routines-phase-2); the table is
      at the end of `src/matched/matched.c`; how routines are proved (the
      private kit and parallel agents) and the next candidates:
      [../handoff.md](../handoff.md). `tools/reimp_names.py` joins the Reimp's
      census (1,535 functions) with this project's regions as a private lead
      list. What the scores measure, and the full "P2 is done when" definition
      (owner, 23-24 and 27 Oct): [progress.md](progress.md#what-is-measured).

      **Done:** 1,496 matched routines registered in batches a to bn (the
      [batch table](#batch-table); bm2 was a fix-only gate). Gameplay score
      97.50%: 157,178 matched bytes a route runs plus 2,631 reviewed
      design-rule original bytes, of 163,907 executed bytes. Every
      route-executed byte of F117.COM, MGRAPHIC and MISC is matched or original
      by design. In the census, named code runs 160,095 of 165,479 creditable
      bytes (1,123 of 1,535 functions wholly, 291 in part).

      **Accounted-for checklist:** 45,390 of 47,433 bytes (95.69%) outside the
      routes have a second-read reference entry (`docs/reference-reviewed.tsv`,
      1,636 entries); 4,363 bytes have none. Four rounds of blind second reads
      by independent readers (26-27 Oct) adopted, from 780 reviewed bytes:
      round 1, 444 units / 19,928 bytes; round 2, 367 / 13,976; round 3, 101 /
      4,476; round 3b (exact leading-range check), 19 / 1,344; round 4, 32 /
      1,245. Round 4 adopted only ACCEPT units whose reader-judged status equals
      the proposal and whose range matches exactly. The reviews add no matching
      credit or exclusions. Evidence `D:/f117-gate/checklist-full-20261026/`.

      **What is left** (owner, 21 Oct: no meaningful game code may be missed):

      - 4,844 executed bytes are unmatched (20 of them design-rule original
        awaiting a reviewed entry), most in the sound drivers; by module:
        `py tools/progress.py`. They include the declined routines (owner,
        23 Oct: still Phase 2 work, not exclusions): VGAME `0010` (`main`),
        START `8427` and END `4393` (wait loops routes cannot finish) and
        MPS_LOGO `1ADC` and `1C82` (unequal); ASOUND.LOG `00FE` (withdrawn in
        batch bj) and ASOUND.117 `025A` (blocked in batch bl). Leads:
        [../handoff.md](../handoff.md); the private list is
        `D:/f117-gate/kit/DECLINED.tsv`.
      - 27,782 matched bytes that only fixtures reach, to be sorted into
        routed or reviewed.
      - The checklist's remaining 4,363 bytes: round 4 left 111 units
        unadopted (36 status differs, 25 range wording, 6 missing fields, 33
        with changes, 11 needs context), which go to round 5 with corrected
        names or added context.
      - Beyond the routes: reviewed static code no route runs (no new route or
        dead certificate yet: indirect/callback/vector and writer closure is
        incomplete, [static-code-review.md](static-code-review.md)); the
        machine-option-only error paths, the probable-dead bytes and START
        `8F94` (the run time's divide-error entry, to be split out of the
        start-up row), each as the owner decided
        ([progress.md](progress.md#phase-2-completion-requirements-owner-23-24-and-27-october-2026));
        the encoded SETUP code; the packer stub's separate review; and the
        closing review of indirect entries.

      **Dead code** leaves the denominator only when certain (owner, 19 Oct), with a
      certificate per row (`docs/census-dead.tsv`, `docs/census-dead-ranges.tsv`).
      The START quiz is an option (`--fix QUIZ`), not dead code, and its routines
      are matched.

      **Stays original code** (census rows in `docs/census-original.tsv`; each
      range also needs a second-read `stays-original` entry in
      `docs/reference-reviewed.tsv`):

      - STI-first interrupt handlers: the timer interrupts VGAME `1C9B`, START
        `8D0B` and END `4BEF`, VGAME's keyboard interrupt `1132:006C` and
        SETUP's static timer handler `104D` (second-read entry, 24 Oct). After
        STI the run loop admits one more instruction before it looks at events,
        so a matched body could do nothing the original does not.
      - The C start-up (START `8EDC` and its copies VGAME `E6BE`, DSWAP `0C7E`,
        SETUP `1516`, END `4DD2`, PLAYER `1096`, MPS_LOGO `0146:0016`): it stops
        after its own STI and never returns, so no lockstep state can complete
        it.
      - The palette loaders with STI inside their colour loop (VGAME
        `14C2:0000`, START `8378`, END `42E4`).
      - The line clipper's divide-error handler (`0FB2:04DD`): fixture-proven,
        but no input reaches it (146,153 real clip divisions across 14 routes
        stay far under its threshold).
      - The program launcher, F117.COM `0100`..`03CA`: it only runs the other
        programs and tests their exit codes (owner, 23 Oct; its overlay loader
        `03CB` is matched).
      - Also by design rule: hardware wait loops only when no lockstep state
        can complete them (none qualifies so far), the packer stub and DSWAP's
        floppy path (no emulated floppy drive).

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

### Batch table

Batches e to bm2 (16-27 October 2026); the routines of earlier batches are
listed by area in the sections after the table. Every batch passed the same
proof (see above): five private seeds per routine with every instruction
reached (more where a row says so), all mutations caught, exact boot (26
checkpoints) and real-route proofs, optimized seed runs, then a fresh full
gate (both engines identical on every route, 63 routes / 62 fresh interpreter
sessions since batch ai; both timing profiles; the matched-routine lockstep,
at three seeds in the current gate; event limits), the N=1 presentation check over every route (45 routes
at batch e) and boot N=4/9. A row's test count is the gate's CTests.
Per-batch evidence is in `D:/f117-gate/p2-batch-<letter>-accept/` (usually
`final-gate-verdict.json`); the commit title carries the scoreboard. Counts are
census bytes by byte credit unless noted; "addresses" is the registered total.

| Batch | Date | Addresses | Added | Routines |
|---|---|---|---|---|
| e | 16 Oct | | 2,062 | VGAME `450A` key dispatch and radar signature; PLAYER `0738` timer picture/fade step; START `038C` launch prompt, `3CEA` modal dialog, `7272` roster save, `A31A` handle write; MPS_LOGO `00DA:0352` sound-overlay loader, `0146:0BCC` stream refill; C runtime init START `8FC6`, END `4EBC` (`sm4_cinit`), MPS_LOGO `0146:010E` |
| f | 16 Oct | | 331 | START `020F` front-end menu loop; PLAYER `0696` intro control poll |
| g | 16 Oct | 967 | 6,181 | 21 routines: VGAME `00F0`, `01C1`, `825D`, `7663`, `C9AE`, `D9FE`, `4657`; START `5BF0`, `2696`, `A015`, `9EB3`, `644A`; END `5825`, `3887`, `099D`, `372F`, `5987`, `34EA`, `2A01`, `0A98`; PLAYER `0D0C`. Added `LIVE_FRAMES` (entries inside a built frame) |
| h | 16 Oct | 985 | 3,894 | 18 routines: VGAME `ED10`, `4973`, `0235`, `EC12`; PLAYER `026B:0272`, `13CE`, `07EA`; START `20D6`, `38AC`, `8DD8`, `141D`, `0B49`, `76D1`, `7312`; END `4CBC`, `1901`, `3CD0`, `4722`. Added `POPF_STOPS` |
| i | 17 Oct | 991 | 602 | END `3F44`, `4648`; START `8782`, `9094` (new `PROGRAM_ENDS` row, alternate-entry return rule); PLAYER `019A` (last two bytes uncredited); VGAME `0FB2:0246` |
| j | 17 Oct | 995 | 1,349 | START `6AB3` roster edit, `0010`; END `2703` medals and ribbons, `0010` (last two bytes uncredited). Also fixed START `76C0`'s RNG bound read order (aliased stack; permanent CTest `matched_start_rnd_stack_alias`) |
| k | 17 Oct | 1,000 | 802 | END `3BF4` (with shared tail `4B8C`), `2265`, `1BAA`; SETUP `111A` (gap `1182` uncredited; `POPF_STOPS` row); START `3EA5` |
| l | 17 Oct | 1,001 | 314 | VGAME `11D8:000A` far picture-to-page wrapper; START `58DF` complete nine-arm grid formatter (replaces its six-instruction opening) |
| m | 17 Oct | 1,002 | 262 | END `2200` awards-page callback; END `22EB` complete ribbon collector (replaces its opening) |
| n | 17 Oct | 1,003 | 1,239 | END `3021` complete debrief scorer (579 instructions, 12-way switch; `34EA` kept as an alias); VGAME `11B8:0008` far picture opener with terminal DOS exit (`PROGRAM_ENDS`, native INT 10h) |
| o | 17 Oct | 1,006 | 268 | END `26A9` report page text; VGAME `D826` set view and sky; START `9E3E` formatter dispatch (any `FATAL_JUMPS` row of a routine now ends its named code) |
| p | 18 Oct | 1,008 | 154 | START `0F4E` briefing page setup; START `35FC` load picture for page |
| q | 18 Oct | 1,010 | 154 | END `1708` and START `31AD` wait-ticks-with-callback helpers; they keep the original unsigned deadline comparison and wrap, so they add names, not a timing fix |
| r | 18 Oct | 1,012 | 142 | VGAME `1A98` driver call with scaled arguments (33 instructions; no route runs its `[0348] != 0` branch, the fixture does; proved on `cargo_pilot`, 127 checkpoints); VGAME `114A:02FF` two far driver calls (23 instructions; `full_cycle`). VGAME `137C:000E` (`1377:005E`) is declined: an observer sits at the same address |
| t | 18 Oct | 1,015 | 962 | VGAME `0FB2:0012` low-detail sky horizon line (164 instructions; the census's `0000:FB32` is the same address, and only `0FB2:0012` is ever entered); VGAME `CE65` key-arm selection (the jump table and key chain of the flight key dispatcher; it hands back to the original arm, so only the chain counts) and `CD7E` panel-state cycle (proved on `strike_training`). The 58 `FATAL_JUMPS` hand-back rows and a `SHARED_ENDINGS` row are in `tests/func_lockstep.c`. Batch s (`0FB2:0012` alone) failed step 7c at a rotating seed and was folded into t |
| u | 19 Oct | 1,016 | 4,552 | VGAME `9EFA` `vgame_display_pass`, the flight display pass (1,628 instructions): weapon markers, gun tracers and their hit tests, the impact blast, the weapon locks, the target and reconnaissance cameras, bomb symbology and the supply-drop radio beacons. Written as named sections (a first mechanical transcript was rejected). Also in this batch: the `display_tour` route (46 routes now), and a harness fix for returns reached through misaligned fuzzed code (CTest `matched_projectile_misaligned_return`) |
| v | 19 Oct | 1,018 | 873 | VGAME `8F71` the navigation display (legs, times and clock bar; 258 instructions) and VGAME `93E9` the armament panel refresh (75): both reached only by the new `display_tour` route (51 checkpoints; 15 and 1 calls). `8F71` misses two instructions there (damage bit 5 of `[3664]`), covered by the fixture |
| w | 19 Oct | 1,019 | 784 | START `1C2F` `start_site_popup_text`: the info popup text of a briefing-map airbase or radar site (303 instructions; two-slot loop; proved on `display_tour`, 51 checkpoints / 40 calls; the route misses 23 instructions that need flagged sites in a theatre briefing, covered by the fixture) |
| x | 19 Oct | 1,020 | 301 | START `11DB` `start_map_hover_light`: the briefing-map hover handler (127 instructions, 301 bytes: relights the palette entry of the item under the pointer, the help line and the site popup; the census rows `11DB`, `1285` and `12DB` are parts of it). Proved on `display_tour` (1,301 calls) |
| z | 19 Oct | 1,020 | 0 (routes and presentation only) | Three routes added (`key_tour`, `quit_dialogs`, `ejection_tass`; 46 to 49 routes, commit `e400c68`) and the rerun copy's orientation change (`d0b7d90`). Gate passed 1,884 s, 257 tests, 49 routes / 48 sessions; `finalize.py`'s credit audit could not run on a batch with no routines (no credit at stake). The first attempt, y, stopped before its gate because the interpreter inputs changed. Batch aa re-gated all 49 routes with its audit passing. Evidence `D:/f117-gate/p2-batch-z-accept/`. |
| aa | 19 Oct | 1,083 | 2,215 | 63 routines on the new routes: the flight key dispatcher's arms (VGAME `C8CA`..`D3F8`: 55 arms, each a hand-back routine named by its key: zoom, countermeasures, gear, flaps, HUD options, the camera and view keys, the height hold, the sensitivity, detail, sound and frame-rate steps, the training switch, the waypoint reset, the display selectors F3-F10, Enter/Backspace buttons and more), the eject arm `CD04`, START's Alt+Q panel `7611` and int86 `9520`, END's Alt+Q panel `3E61`, TASS page `3BBF`, int86 `519E` and the whole icon-frame switch `0977`. On `key_tour`, `quit_dialogs`, `ejection_tass`, `display_tour`, `navigation_grid`, `recon`, `airair_type5`, `korea_strike` and `camera_gradients`. Mutation counts: 1,709 in all, all caught; 13 arms of two or three instructions have fewer than 20 (5 to 18) because no more distinct edits exist |
| ab | 19 Oct | 1,120 | 2,040 | 37 small routines on existing routes, found by four module-group proof agents: SETUP (cursor, shut-down, tick wait, timer-hook install), VGAME (display-page registration, background span fill, picture close/read, frame loop), END (file reads, allocate-or-quit, debrief picture, DOS open, exit), START (hangar scene, the six objective-sentence arms, file loads, buffer allocation, clock hook, long-to-text, malloc), MPS_LOGO and PLAYER (palette write/read, allocators, break-vector hook, timer install, clock-hook step). Also the playback-clock default 0.4 / 0.025. Dropped: END `4160` (diverged on a real route), `23D6` and `3682` (they replace existing fragment rows), VGAME `0010`, `12A11`, END `03D9`, `57B0`, MPS_LOGO `067C`. 1,029 mutations, all caught |
| ac | 20 Oct | 1,125 | 290 | 5 routines: END `4160` (DOS block read: its first proof hard-coded END's data segment, which the loader relocates, and failed on `landing`; fixed by reading the immediate from the code), END `2430` and `36D6` (the tails of the ribbon and death-message routines, registered beside their existing fragment rows), MPS_LOGO `0030` (`fread` of bytes into a far buffer) and PLAYER `01F6` (`player_finish`). 127 mutations, all caught. Also: `/Os` on `matched.c` (`/O2` took 90+ minutes to compile it from batch ab on), and 3,299 bytes certified dead left the denominator (`docs/census-dead.tsv`) |
| ad | 20 Oct | 1,125 | 0 (routes) | seven new routes, no new routine: five damaged-install routes (`err_player_assert`, `err_player_divide`, `err_boot_series_palette`, `err_driver_overlay`, `err_dswap_nostart`: the original programs' own error handlers run against a private copy of the install with one file removed or damaged, `tools/route_damage.py`) and two compressed-boot flights (`libya_tour_eject`, `korea_release_eject`). 56 routes / 55 sessions identical between the engines. They run about 3,250 more census bytes (128 rows) that no routine covers yet. Gate 2,184 s (the compile took 5 minutes with `/Os`) |
| ae | 20 Oct | 1,125 | 0 (option) | the START quiz as an option: `--fix QUIZ` (an override at START `001D` that pushes `0020` and runs the quiz routine `0703`, the call GOG replaced with three NOPs; off by default, `all` and normal play never switch it on) and the routes `quiz_right` / `quiz_wrong` (both verdict arms, `planes2.pic` milestone). The 633 quiz bytes left `docs/census-dead.tsv` and are back in the denominator, so P2 dipped from 88.38% to 88.09%. 58 routes / 57 sessions identical; gate 2,305 s |
| af | 20 Oct | 1,148 | 697 census credits | 23 routines: DSWAP `0647`, `0668`, `06EA`, `08F4`, `10F4` (close/read, picture block/rows, BIOS disk); END `00C7`, `00C8`, `22EA`, `47FE`, `2BCB`, `42BE`, `451B` (empty hooks, free-or-quit, palette, mouse); PLAYER `1C12`, `1D8D`, `1F54` (string/integer formatting and abort); SETUP `1509` (DOS string); START `057B`, `057C`, `885E` and VGAME `11D8:0155`, `02C2`, `02C3` (empty hooks); VGAME `1BC2` (flight-loop wrapper). 363 fully covered instructions, 550 caught mutations, 115 optimized seed checks; 385 CTests and all 58 routes pass. Gate 1,555 s; acceptance 1,689 s with presentation overlap. Serial/parallel translation is byte-identical; coverage loading 51.23 to 5.20 s. The harness span alias and the ROM-free scheduling test's install dependency are repaired. |
| ag | 20 Oct | 1,162 | 645 named bytes | 14 routines from the rows that routes run but no routine covered: MPS_LOGO `0045:003A` (string position), `0045:0102` (DOS block free), `0146:0FE4` (formatter scan), `0146:15FA` (`_write`, with its shared tails), `0146:17B0` (checked allocation), `0146:1A64` (far jump to the allocator); PLAYER `1FD0` (the C run-time's stack check), `22AF` (raise()'s exit(3) tail); END `5948` (`%d` arm), `3692` (debrief message arm); DSWAP `0599` (DOS open or report), `0848` (colour registers); VGAME `104E:0082` (far sine entry), `1058:000E` (far HUD colour setup). 313 fully covered instructions, 341 caught mutations, 70 optimized seed checks; 399 CTests, 58 identical routes / 57 fresh interpreter sessions. Gate 1,604 s, acceptance 1,731 s with presentation overlap. Five more proofs were held back: MPS_LOGO `27C2` and `163A` (their real-route proof found no call on the routes tried), VGAME `14C2:00B7`, `1058:000A` and `1058:0012` (the lockstep span table was keyed by IP, so the new rows would have aliased same-IP routines in other segments; batch ah added the segment key). Blocked as not routines or already matched: PLAYER `1BBA`, `1D50`, `1C3D`, `1D02` (entries inside the printf dispatcher `1BB6`, whose own proof has no verdict), `222D` (a switch table), `1FCF` (padding), END `4FA4` (run inline by `end_exit`), VGAME `11322`, `11372`, `11ED5` (already matched under other entries), MPS_LOGO `039A` (already matched). Hand-back routines `2444`, `2c10` and the formatter and runtime bodies whose block macros the private audit cannot enumerate are marked partial for that audit only; credit is by byte coverage. |
| ah | 20 Oct | 1,168 | 787 named bytes | 6 routines: the START quiz (`057E` draws the screen, `0703` takes the pick and judges it, `0786` highlights the hovered button; the census rows `0583`, `06E0`, `072E` and `0788` are mid-instruction pieces of these three, credited by byte; proved on `quiz_right` with `--fix QUIZ`), PLAYER `1BB6` (printf's conversion dispatch, 471 bytes, 65 instructions; seeds now compare 6,124-6,146 states each, the earlier proof's seeds had skipped nearly all; it also credits the census rows `1BBA` and `1D50`, whose first bytes are mid-instruction), VGAME `1058:000A` and `1058:0012` (far entries of the HUD pass and layout switch). 328 fully covered instructions, 275 caught mutations, 30 optimized seed checks; 405 CTests, 58 identical routes / 57 fresh interpreter sessions. Gate 1,783 s, acceptance 1,729 s. The lockstep span table gained a segment-keyed companion (`SPANS_SEG`) because `1058:000A` and `1058:0012` share addresses with routines in other segments. VGAME `14C2:00B7` was proved too but is the same code as the existing `1452:07B7` (linear 14CD7), so it was dropped; a first gate run caught the duplicate through its routine tally. Three claimed-but-uncredited bytes come from `1BB6`'s shared tail at `1D52`, entered from two arms. |
| ai | 20 Oct | 1,168 | 41 census bytes (routes) | five new damaged-install routes, no new routine: `err_start_no_world` (LB.WLD removed: START's run-time error banner), `err_start_terrain_files` (LB.3DG removed, LB.3DT magic zeroed), `err_vgame_no_photo` (PHOTO.3DX removed: VGAME's file-error arm, a key already pressed so the matched `getch` never waits), `err_dswap_no_swap_palette` (LABSLOGO.SS, START.EXE and SWAP.PAL removed: MPS_LOGO's not-found arm and DSWAP's fatal-text printer) and `err_end_no_tass` (TASS.PIC removed after the ejection flow); 63 routes / 62 sessions identical between the engines; together they run 581 never-run census code bytes that no routine covers yet. Also: VGAME `D9E7` (set the scene origin) read its three arguments before its stores, but the original reads each just before its store: the commit-keyed seed of step 7c found it when DS and SS overlap (CTest `matched_scene_origin_stack_alias`); the dead-list row VGAME `0EBEA` (9 bytes) was removed because a route reaches it; `tools/presentation_check.py` names the two routes that start VGAME but end at a file error as `NO_FLIGHT_ROUTES`. Four gate attempts: one 7c mismatch (`D9E7`), one event-limit overrun (an open `getch` wait: a key typed after 1.4M clocks waits, before 1.0M does not), one presentation pairing requirement, then pass. Gate and acceptance 1,658 s, 406 CTests. |
| aj | 20 Oct | 1,172 | 277 named bytes | 4 routines on the new damaged-install routes: DSWAP `0754` (the fatal-error text tail: clear the screen, print the file name and message, end the program), MPS_LOGO `0146:1364` (the formatter's field output: sign, padding and text; the census row `27C2`/`1362` is two bytes of jump in front of it that no route takes), VGAME `0CDD` (show a message line and wait for a key) and END `41CA` (the fatal-error screen; the census row `419A` is a mid-instruction piece beside it). 116 fully covered instructions, 122 caught mutations, 20 optimized seed checks; 410 CTests. Gate 1,770 s, acceptance 1,753 s with presentation overlap (a keyed baseline for 63 routes now exists). First attempt failed at the commit-keyed seed: the START quiz judge's fixture matched the pick to the answer only by chance, so one seed never ran the right-answer arm; it now forces both verdicts in every fourth state each. Of about 25 rows given to proof agents from the new routes, 21 were already matched under other names or are not routines (START `09800` `09848` `09873` `07D71` `07E88` `0963E`, MPS_LOGO `007E2` `02CF4` `021D2` `0171A`, VGAME `0F5F2` `0895E` `009A0` `0E586` `0F1E0`, END `0400F`, and shared DOS tails). |
| ak | 21 Oct | 1,181 | 875 named bytes | 9 routines in rows that routes run but no routine covered: DSWAP `023E` (draw the disk-swap screen), `03A7` (open a picture file, decode it onto a page, close it), `0AD5` (the picture decoder's start) and `0BCF` (its code and table step: it leaves by `JMP BP`, handled by a `run_to_ret` rule and the pop-distance exemption); MPS_LOGO `0006:0044` (clear the text screen), `0006:000C` (`fatal_error`) and `0146:01D3` (the whole `exit()` entry through the shared tail; `01DA`/`01E2` are entered by no route, so the earlier `_exit` proof stays private); END `3B9A` (put up the report picture) and `03D9` (debriefing menu, rating and tally: D12 has two fix sites inside, so the body declines while D12 is on, like five other bodies). 379 fully covered instructions, 283 caught mutations, 45 optimized seed checks; 419 CTests. Gate 1,719 s, acceptance 1,606 s with presentation overlap. Fixtures force each branch outcome in a fixed share of states and were run at six extra seeds. The private audit needed two allowances: bodies without block macros take the placeholder prefix, and a hook inside the routine is accepted when a covering `fix_inside` guard declines it. |
| al | 21 Oct | 1,189 | 495 named bytes | 8 PLAYER routines in rows that routes run but no routine covered: `1A7E` (printf's format scan, with the stack check inlined), `18C4` (`fprintf`), `1476` (assert failure), `1839` (write a message to the console), `15BA` (start-up check failed), `223A` (`raise`: it hands its tail to the already matched `22AF`, so no double credit), `026B:0244` (program the timer chip; a segment-keyed span and a `POPF_STOPS` row because `0000:0244` is another routine) and `0010` (`main`; the dossier span was one byte short of its RET, which had skipped every state). 228 fully covered instructions, 255 caught mutations, 40 optimized seed checks; 427 CTests. Gate 1,579 s, acceptance 1,549 s with overlap. Four claimed-but-uncredited bytes (inlined stack check). The batch copy of the private audit accepts a call inside a block for a `partial` body (the lockstep compares the clocks). |
| am | 21 Oct | 1,193 | 114 named bytes | the pilot outside the seven programs, four routines in the overlay modules: MISC `0007:0000` (DOS console input with echo), `000A` (BIOS key-waiting test, INT 16h) and `0018` (clear the BIOS shift flags), and MGRAPHIC `019C:023A` (a character's width in a font; it reads its relocated data-segment word from the loaded code, and the proof fixture patches that word to four values so a hard-coded 0 is caught). The module tokens for the launcher, graphics library and sound drivers are defined in `matched.c`. 55 fully covered instructions, 124 caught mutations, 20 optimized seed checks; 432 CTests. Gate 1,586 s, acceptance 1,572 s. The commit-keyed seed also exposed a thin fixture in the batch t sky routine `0FB2:0012` (162 of 164 instructions at one seed): its fixture now forces the missed arm in one state in four (22 seeds checked; its 69 mutations were re-run against the new fixture and all 69 are still caught, `D:/f117-gate/p2-vgame-fb32-recheck/`). The two INT-wrapper fixtures answer INT 16h/21h per state through a new harness switch (`g_kbd_force`). |
| an | 21 Oct | 1,207 | 416 named bytes | 14 routines outside the seven programs: MISC `0029` (game-port joystick button test, proved on `setup_calibration`), F117.COM `03CB` (the launcher's overlay loader: the first launcher routine; it proves `.COM` addressing and CS-relative loads on a real load; a `PROGRAM_ENDS` row for its error arms and a narrowed code-written window, because its variables follow its code), MGRAPHIC far thunks `0203` `020E` `0219` `0224` `022F` and the frame thunk `019A`, and MGRAPHIC's drawing-surface getters and setters `07C3` `07C8` `07CD` `07E4` `080F` and table read `12BA`. 178 fully covered instructions, 424 caught mutations, 70 optimized seed checks; 446 CTests. Gate 1,728 s. |
| ao | 21 Oct | 1,221 | 445 named bytes | 14 MGRAPHIC routines (`0581`, `0C5D`, `11F1`, `0EE6`, `0ED5`, `1131`, `07AC`, `055B`, `07FE`, `07D5`, `0791`, `0814`, `1251`, `12E0`), with the stricter harness: the clock folded into every port-access check and bodies required to stop at a timer/PIC wake write; 348 mutations, 1,854 s |
| ap | 22 Oct | 1,227 | 178 named bytes | 6 MGRAPHIC routines: `07A3` (copy a CS word), `12D5` (ES from a CS segment table), `12DD` and `12DF` (bare far returns), `0E30` (clip-and-blit entry stub) and `0E41` (clip a blit rectangle against two bounds per axis, then far-call `0EE6`); 110 mutations, 1,719 s. The gate's step 7c at the commit seed found a harness gap, not a routine fault: the nested run of original code a body calls followed a fault vector to the clock limit while the oracle stops at the first fault (VGAME `5852`, fixed in `e6464c9`) |
| aq | 22 Oct | 1,236 | 244 named bytes | 9 MGRAPHIC routines: `04A4` (the largest free DOS block), `04B6` (enter mode 13h and check it took), `04CA` and `04E4` (screen off and on, each after a vertical retrace), `04FE` (allocate the picture arena or return a page segment), `057A` (the pixel plot's tail, entered from outside: routes never enter `056E`), `0826` (return the constant 3; only `key_tour` runs it), `09A8` (take the largest free block for the arena) and `1118` (clear a 64,000-byte page). `0561`, `07B2`, `07DB` and `0804` are inner entries of matched bodies, credited once there. 278 mutations, 1,623 s. The harness gained an I/O answer salt, a settable INT 10h mode answer and a return frame for `057A` |
| ar | 22 Oct | 1,245 | 1,500 named bytes | 9 larger MGRAPHIC routines: `0599` (a Bresenham line; it runs the pixel plot's tail `057A` and `0581` inline), `071A` (dashed rows), `09D4` (release the arena `09A8` took), `0B0C` (compile a bitmap into rows through `0A02`, `0A6A`, `0A4D`), `0B70` (clip a sprite and draw it), `0C95` (a scaled, mirrored, clipped run-length sprite; it reads the two bytes it patches into its own code), `0F3D` (horizontal span fills in four modes; routes cover under half of it), `1188` (recolour in a rectangle) and `125C` (palette flash and screen jolt). 491 mutations, 1,665 s. A 13-seed sweep before the gate found two older defects, fixed in the same commit: VGAME `1377:0874` and its three entries ran on past a graphics-controller OUT that wakes the run loop, and VGAME `120A:0815` had a self-write window that missed its escape code below the entry |
| as | 22 Oct | 1,248 | 263 named bytes | MGRAPHIC `0A02` (open the bitmap buffer and write its dimensions), `0A4D` (close it and advance the arena by its rounded paragraph count) and `0A6A` (encode a row, trimming leading and trailing zeros). All 94 instructions covered, eleven private seeds per routine, all 79 mutations caught; exact 66-checkpoint game and 26-checkpoint boot proofs. Three whole-table pre-gate seeds passed; 489 CTests; acceptance 1,621 s. |
| at | 22 Oct | 1,251 | 429 named bytes | MGRAPHIC `0BC0` (clip sprite bounds), `061A` (draw a font string: colour controls, variable widths, clipping, opaque/transparent glyphs) and `0F3B` (the one-instruction mode-zero prefix to `0F3D`, with its continuation left to the existing code). All 161 implemented instructions covered, eleven private seeds per entry, 108 mutations caught. The bounded prefix earns only its two bytes, and its wrong-flag mutants are caught. All 1,248 existing routine reports remained identical at seed `0x27182818`. Three pre-gate whole-table seeds and 492 CTests passed; acceptance 1,595 s. |
| au | 22 Oct | 1,256 | 330 named bytes | MGRAPHIC text-string cluster: `02B9` and `037F` (clip helpers), `03D1` (the shared far-return tail), `03E8` (shared set-up, leaves by JMP SI) and `03C9` (a front end). All 130 implemented instructions covered, 212 mutations caught, exact game and boot proofs on routes that run every branch (`central_america_ground_training` for `02B9` and `03D1`, `cargo_pilot` for `03E8`, `ibm_boot` for `03C9`, `full_cycle` for `037F`; `full_cycle` misses the rare arms of the others). `02B9`, `037F`, `03E8` and `03C9` are audited as partial: a rejected string leaves through the shared `03D1` tail (run inline or handed back to), `03E8` leaves a dead restore arm unwritten, `03C9` calls the original `03E8`. All 1,251 existing routine reports unchanged at seed `0x27182818`; six pre-gate whole-table seeds clean. 497 CTests; acceptance 1,920 s. |
| av | 22 Oct | 1,261 | 129 named bytes | MGRAPHIC text-string cluster, the rest: `031D` (clip B), and the front ends `02A1`, `02B0`, `0314`, `0376`. All 51 implemented instructions covered, 211 mutations caught, exact game and boot proofs (`persian_gulf_air_training` for `031D`, whose cut arm `full_cycle` never reaches; `full_cycle` for the front ends). All five are audited as partial (they call the original `03E8` or hand back at `03D1`/`03CC`); the private audit ran before the gate this time. All 1,256 existing routine reports unchanged at seed `0x27182818`; six pre-gate whole-table seeds clean. Every route-executed MGRAPHIC byte is now matched. 502 CTests; acceptance 1,727 s. |
| aw | 22 Oct | 1,268 | 945 named bytes | START selection change `5E6F`, theatre page `5A8A`, message panel `6045` and panel rectangles `5FFA`; DSWAP `2097` and SETUP `1D69` near-heap malloc; PLAYER `0520` video-mode query. All 458 instructions covered, all 372 mutations caught. The private route proofs are thin for `5A8A`/`5FFA` (once each), `6045` (three times) and `1D69` (twice); the full gate runs each in 27 or more sessions. The theatre page has 35 fixture-only instructions. Six whole-table pre-gate seeds clean; all 1,261 old reports unchanged at `0x27182818`. 509 CTests; acceptance 1,671 s. |
| ax | 23 Oct | 1,287 | 271 named bytes | 19 leaf routines: START cel steps, BIOS ticks, mouse position, flush count and free-or-exit; END ticks; MISC BIOS key read; DSWAP flush count, current drive and empty picture hook; SETUP calibration counters; MPS_LOGO video mode, exit checksum and formatter word/pointer readers; PLAYER picture close/end flag and interrupt-vector get/set. All 141 instructions covered, 344 caught mutations, 95 optimized seed checks. Six whole-table prechecks pass: 1,266 old reports identical, two changes explained by exact callee-return recovery and the existing mid-run probe's random draw. The harness now rejects contradictory unfinished I/O, checks all INT clocks and recognizes exact original returns with extra stack pops without granting completion credit; 19 synthetic cases pass. 528 CTests. Added gameplay credit 230 bytes, fixture-only credit 41; core gate 1,584 s, acceptance 1,795 s. |
| loadbase | 23 Oct | 1,287 | 0 (infrastructure) | Relocation proof infrastructure, no new routine: the harness loads the launcher, overlays and sound drivers at 0800h (CLI controls permit zero or 0400h..0800h), applies generated relocation metadata at both image reloads and compares named coverage against relocated bytes; the seven ordinary programs keep their historical zero base. All 1,287 zero-base reports identical; all 73 affected entries pass at 0400h and 0800h without skips; a planted DS=0 mutant invisible at zero is caught at both nonzero bases. A graphics fixture's empty renderer page table was repaired after a nonzero-base trace showed code being overwritten (a fixture repair, not a game bug). The new CMake loader target changed the interpreter key, so the closing audit now checks the fresh references the fallback actually used. 529 CTests, six whole-table precheck seeds; core gate 1,369 s. Evidence also in `D:/f117-gate/modules-loadbase-20261022/` |
| ay | 23 Oct | 1,290 | 1,114 named bytes | First sound-driver overlay routines: RSOUND.LOG `021B` (private-stack wrapper around two tick callees, `roland_boot`), ASOUND.LOG `0344` (AdLib timer tick service, `full_cycle`) and ISOUND.LOG `0253` (PC-speaker sequencer tick, `ibm_boot`). 420 instructions covered, all 199 mutations caught. `0344` and `0253` are audited as partial: each stops at the counter-0 control write (a run-loop wake) and the original finishes the reload, STI and RET. Real routes run only part of `0344` and `0253` (no route reaches the sweep arms or the stream readers); lockstep holds the rest. Two harness hunks: a coverage-origin line for each of the two, and a self-write exemption for ISOUND.LOG's call counter at `cs:0251`. Gameplay credit +698 bytes, fixture-only +416 (named code no route runs joins the all-code scope). Whole-table pre-check at `0xadb6a420a86f`: 1,290 routines, 0 mismatching. 532 CTests; acceptance 1,896 s. |
| az | 23 Oct | 1,303 | 849 named bytes | First sound-driver leaf families, 13 routines in one gate: ASOUND.LOG `0515` (voice-record reset), `058F` (resets six records; hands back at its tail jump into `0515`, which runs as its own matched callee), `0A1C` and `0B30` (OPL operator-register writers, stubbed `02F6`) and `0333` (noise step, far return); ASOUND.117 `03C4` (its overlay twin); ISOUND.117 `0163` (sequencer and speaker-gate reset) and `0140` (song-state setup, calls the original `0168`); RSOUND.LOG `0171` (sequencer state, 63-iteration loop handed back at the loop head), `040C` and `020A`; RSOUND.117 `0B42` and `0B9C`. 272 instructions covered, all 366 mutations caught (21,535 real route calls for `03C4`, 8,155 for `0B9C`). Audit marks `058F`, `0140`, `0163` and `0171` partial. **Defect fixed:** the commit-keyed pre-check seed exposed START `28CE` (batch ax): the body read all four stack arguments up front, but the original reads `count` after it has stored `x`, so a stack overlapping the data segment gave a different AX (`28CE` now reads each argument through BP in the original's order; 14 seeds of 20,000 states and two whole-table seeds clean). 545 CTests; acceptance 1,897 s. |
| ba | 23 Oct | 1,322 | 1,048 named bytes | 19 sound-driver routines in one gate: ASOUND.LOG `019A` (chip init, 255 OPL register writes through stubbed `0300`) and the OPL register-writer family `07DC` `09B6` `0A00` `0A62` `0AAA` `0AEC` `0BB8` (stubbed `02F6`); ASOUND.117 `0839` and `05A5`; ISOUND.117 `0008`, `05A4`, `0626` and `0102` (speaker noise/vibrato tick with a forced vibrato arm); ISOUND.LOG `010B`; RSOUND.LOG `02BE`, `0580` and `0FA2` (six voice records, `08BE` stubbed); RSOUND.117 `0E2A`. 397 instructions covered, all 595 mutations caught (23,898 real route calls for `0102`, 53,520 for `0839`; `02BE`, `0008` and `010B`'s sibling arms are reached once or by lockstep only). Eight bodies are audited as partial (calls into original or already-matched code, claims the audit cannot count). **Defect fixed:** the commit-keyed seed exposed END `100D` (batch ad era): its block was checked with `room(c, 8)` but a CALL followed, and `guest_call` charges the CALL itself, so a stop limit landing exactly there ran the CALL one instruction late; the three call sites now reserve `+ 1`. A scan found 39 further bodies with the same shape (many false positives: bodies that claim the call in a separate `SM4_NEED`); all reserve `+ 1` now, which can only make a body decline sooner. 564 CTests; acceptance 1,991 s. |
| bb | 24 Oct | 1,355 | 1,623 named bytes | 33 more sound-driver routines in one gate: ASOUND.LOG `0640` `06AA` `0BE2` (eight calls to the matched OPL writers) `05DA` `0618` `05F7` (open/close/dispatch entries, relocated segment words, `05F7`'s indirect table call proved whole); ASOUND.117 `0E5E` family (`0DCE` `0D1C` `0C2E`), `03D8`/`03D5` (one body, claimed once), `0B90`, and the voice-reset group `05E7` with six thunk prefixes (`05E3` `05DC` `05D5` `05CE` `05C7` `05C0`, registered at the jump-only entry `05E7`); RSOUND.117 voice-start family (`0EDB` owns the shared tail, `0EBD` `0EC7` `0ED1` as bounded prefixes), `0444` `046D` `03D6` (stub `0C7C`); RSOUND.LOG `05F6` `05CC` `0611`; ISOUND.LOG `0051`; ISOUND.117 `008E` `00B5`. 641 instructions covered, all 877 mutations caught. **Two audit catches, both fixed before the gate:** `05F7`'s two exit paths both claimed the shared tail at `0616`, which the disjointness check rejects — restructured to a single shared label; `05F6`'s fixture let two of its table's real targets (`0458`, `048E`) run as original code half the time, which would have credited their bytes to `05F6` without a proof — the fixture now always stubs them. Also added the sixth design rule "hardware wait loop" (unused: a first read found all 8 candidate loops can be completed by a fixture, so they were matched instead of exempted) and F117.COM's launcher as a reviewed "program launcher" original entry. 597 CTests; acceptance 2,032 s. |
| bc | 24 Oct | 1,359 | 170 named bytes | Four installer routines in the sound-driver overlays: ASOUND.LOG `005A`, ISOUND.LOG `060E`, RSOUND.LOG `005B` and its separate one-instruction return `00AD` (`full_cycle`, `ibm_boot`, `roland_boot`). 57 instructions covered, five private seeds plus a fresh one per routine, all 80 mutants caught. Three `POPF_STOPS` rows; the hand-backs are audited as partial, `00AD` uses `NOBLOCKS`. RSOUND.LOG's already-installed branch targets the STI at `00AC`, while ASOUND.LOG `005A` and ISOUND.LOG `060E` skip STI on theirs. Also: the launcher reference corrected to guest range `0100..03CA` (249 executed bytes recognized), runtime-patched singleton jumps validated (`docs/census-runtime-jumps.tsv`; [progress.md](progress.md#capture-validation-and-reference-ranges)), and the ISOUND.LOG `0253` fixture repaired after a coverage floor at precheck seed `0x600DF00D` (reproved separately, no new credit). 601 CTests; gate 1,741 s. Commit `51a70f1` |
| bd | 24 Oct | 1,383 | 864 named bytes | 24 sound-driver routines: ASOUND.117 `041E` `0424` `0634` `0647` `0686` `075D` `08B0` `0BF4` (record-reset thunks into the matched voice-reset body, and leaves); ISOUND.LOG `006E` `0161` `066C`; RSOUND.117 `0490` `04E1` (MIDI volume/pan), `0E95` `0EB3` `10CF` `1119` (voice-start prefixes), `11F9` (MPU driver start), `1309`, `0A6B` (hook INT 0Ah once); RSOUND.LOG `063C` `06F4` `0754` `0860`. 314 instructions, 574 mutants caught; gate 625 CTests, acceptance 1,836 s. Four proved ISOUND.LOG timer-path entries (`0603` `060D` `0635` `0663`) were held back: registering them made `err_vgame_no_photo` differ between engines. The cause was later traced to an engine defect, not the bodies (a stale module instance, below; fix and the four entries in batch bd2). Evidence `D:/f117-gate/p2-batch-bd-accept/`, commit `4cf1600`. |
| bd2 | 25 Oct | 1,387 | 64 named bytes | The stale-override engine fix (below) and bd's four held-back ISOUND.LOG timer-path entries `0603` `060D` `0635` `0663`. The first gate attempt failed the `quiz_*` routes: the check compared packed START.EXE file bytes and declined the QUIZ override, so it now reads the translator's unpacked image (`attempt1/`). 630 CTests; acceptance 1,883 s. Evidence `D:/f117-gate/p2-batch-bd2-accept/`. |
| be | 25 Oct | 1,396 | 73 named bytes | Eight ASOUND.117 command thunks (`0899` `08C2` `0892` `08D0` `090E` `08AD` `08BB` `088B`) and RSOUND.LOG `00AE` timer uninstall. 39 fixture instructions, 212 mutations caught; 45 optimized seed checks; 73 new route-executed bytes, fixture-only bytes unchanged. Also repairs VGAME `EDC2`'s scan-before-PUSH-BP alias (clean-master failing control, unchanged fixture/harness), and the measured D101/Q12b fixes. Two whole-table pre-check seeds pass; 640 CTests. Core gate 1,338 s; batch acceptance 1,802 s; combined acceptance including repeated fix/default checks 1,948 s. N=1 260,613 frames / 260,527 pairs; boot N=4/9 736 / 732. Evidence `D:/f117-gate/p2-batch-be-accept/`, `d101-q12-measure-20261025/`, `edc2-master-20261025/`; commit `a172459`, [CI passed](https://github.com/TideGear/OpenNighthawk/actions/runs/40259878238). |
| bf | 25 Oct | 1,400 | 61 named bytes | RSOUND.LOG one-shot timer installer in four entries: `0251` (guard, vector, PIT0 control write; 15 instructions), `027C` (reload low/high bytes; stops after the second write), `0289` (POP AX, STI; stops after the STI), `028D` (RET). 25 fixture instructions, 46 mutations caught; 20 optimized seed checks. Two whole-table pre-check seeds (`0x4fc6ba6c9f0d`, `0x5EED0BF1`) pass. 644 CTests; acceptance 1,642 s. Evidence `D:/f117-gate/p2-batch-bf-accept/`. |
| bg | 26 Oct | 1,425 | 473 named bytes | 25 sound-driver entries in one gate (the owner's 26 Oct plan: fewer, larger gates), all route-executed: ISOUND.117 `0065` `05B7`; ASOUND.117 `1929` `193E` `033A` `0B57`, the speaker-timer pair `0B74` `0B75` and seven wrappers `0887` `088E` `0895` `08B7` `08D3` `08DA` `08FC`; ASOUND.LOG `00DA` `00FA` `0423` `042C` `0300` `02F6`; RSOUND.117 `0C43` `0C7C`; RSOUND.LOG `02D4` `0310`. 255 fixture instructions, 427 mutations caught, 125 optimized seed checks; two whole-table pre-check seeds (`0xaebbf86cfffe`, `0x5EED0BF2`) zero mismatches. The six MPU/OPL wait-loop fixtures force timeout and success by state index. Gate 1,970 s. Evidence `D:/f117-gate/p2-batch-bg-accept/`. |
| bh | 26 Oct | 1,430 | 1,410 named bytes | The five larger bg4 routines, all route-executed: ASOUND.117 `1048` (operator register emit, 205 instructions) and `123E` (instrument pair load), ASOUND.LOG `0BFC` (operator loader, 110 instructions), ISOUND.117 `062E` (effect-record selector, 128 instructions) and `0443` (word effect decoder). 614 fixture instructions, 188 mutations caught, 25 optimized seed checks; three whole-table pre-check seeds (`0x5295f6b75bad`, `0x5EED0BF3`, `0x20261027`) zero mismatches. The fixtures of the accepted VGAME `4973` and ASOUND.117 `0C2E` were corrected (a macro-local name clash had made their written values address-derived) and re-proved. 674 CTests; gate 1,723 s. Evidence `D:/f117-gate/p2-batch-bh-accept/`. |
| bi | 26 Oct | 1,432 | 1,393 named bytes | ASOUND.117 `0E8A` (AdLib voice change, 170 instructions) and RSOUND.117 `0002` (Roland per-voice sequencer step, 372 instructions, jump-table arms; it declines while the Q16 fix at `0348` is installed, `fix_inside`). 542 fixture instructions, 122 mutations caught. The private audit needed two batch-local widenings: an `XCALL(target,ip,ret)` claim form, and the declared SPANS row for overlay routines, whose indirect arms the static dossier does not list. 676 CTests; gate 1,714 s. Evidence `D:/f117-gate/p2-batch-bi-accept/`. |
| bj | 26 Oct | 1,436 | 283 named bytes | ASOUND.117 `0720` and `0694` (voice-record reset wrappers; `0694` hands back at the `05C0` thunk), ASOUND.LOG `0140` (AdLib detect and init, 90 bytes) and RSOUND.117 `050A` (steps the eight voices through `0002`). 107 fixture instructions, 124 mutations caught. 680 CTests; gate 1,694 s. ASOUND.LOG `00FE` (delay-loop calibration, proved privately) was **withdrawn**: its lockstep stubbed the PIT sampler `00DA`, and on the real `setup_calibration` route the matched body took the timer IRQ at a different boundary after its nested `call 00DA` (hash `0edb...` against the interpreter's `4a39...`); it stays original until proved against the real sampler. Evidence `D:/f117-gate/p2-batch-bj-accept/`; the failed gate with `00FE` is `p2-batch-bj-failed-00FE-20261026/`. |
| bk | 27 Oct | 1,444 | 418 named bytes | ASOUND.117 `1882` `0562` `0C1C`, ASOUND.LOG `116E`, ISOUND.LOG `00A2`, RSOUND.LOG `0349`, RSOUND.117 `0E4C` `0CAE`, all route-executed. 140 fixture instructions, 228 mutations caught, 40 optimized seed checks. The AdLib noise fixture patched the wrong jump operand and was corrected before proving; the pitch-bend fixture saves FLAGS before logging, reads the return stack through SS and recognizes its tail-called writer's outer RET. Three whole-table pre-check seeds and all 53 reached routes (matched routines disabled) pass. 688 CTests; acceptance 1,659 s. Evidence `D:/f117-gate/p2-batch-bk-accept/`, independent private-proof verification `root-private-verification.json`. |
| bl | 27 Oct | 1,477 | 892 named bytes | 33 routines from three proof agents working side by side: eight Roland (RSOUND.LOG `0390` `0306` `03FA`, RSOUND.117 `0CF5` `0C72` `0E70` `112F` `0C2D`), two ISOUND.117 (`0079` `0045`), six ASOUND.117 helpers (`0BD4` `0C08` `0B76` `197B` `018F` `0210`) and 17 ASOUND.117 song/voice-start wrappers (`064D` `0670` `0876` `08E8` `089C` `085F` `0903` `0640` `0662` `0669` `068D` `06BE` `06E8` `0704` `08BE` `08CC` `08E1`). 334 fixture instructions, 900 mutations caught, 165 optimized seed checks. `0306` and `0C72` are 10-byte hand-backs into the already matched MPS writers; `089C` needed a `g_cov_ip` row for its lone RET below the entry; `0E70` needed the `0E4C` outer-RET recognition extended; `0045` and `0BD4` needed the native INT 1A function 2 answer. ASOUND.117 `025A` was **blocked**: it ends in a loop that polls six voice bytes only the timer interrupt clears, so no lockstep state reaches its RET. Pre-checks: two whole-table seeds (`0xa1bc7fe770f2`, `0x5EED0B12`; 5.83 M states each, zero mismatches) and all 49 reached routes agree with matched routines disabled (`route_precheck.py` on all 33 entries; `D:/f117-gate/bl-prechecks/`). 721 CTests; acceptance 2,078 s. P2 92.59% -> 93.01%; unmatched executed bytes 7,537 -> 6,659. Evidence `D:/f117-gate/p2-batch-bl-accept/`. |
| bm | 27 Oct | 1,487 | 3,018 named bytes | Ten routines from twelve proof agents: RSOUND.117 `1225`, ASOUND.117 `1BCC` `1BAF`, ISOUND.117 `0023` `07CF`, END `57B0` (the 92-byte dispatch arm; 11 `FATAL_JUMPS` rows), ISOUND.LOG `00B8` (dispatch logic only: its arms are stubbed), ASOUND.LOG `0804` and `0D1A` (partial, `107C` unclaimed), ASOUND.117 `1360` (one body for its 13 arms). 1,036 fixture instructions, 313 mutations caught. 731 tests; gate 1,797 s. Six bytes are claimed but uncredited: the two exits from END `57B0` into other arms. Held: START `08F94` (no census row covers its bytes yet), RSOUND.LOG `08BE` partial (guard review). Evidence `D:/f117-gate/p2-batch-bm-accept/`. |
| bm2 | 27 Oct | 1,487 | 0 (fix only) | Call-budget harness repair: after a call into original code only the exact returned clock is exempt, never later C instructions. It found two accepted bodies under-claiming room, both ours: START `8CA8` (no room claimed before its two INT 21h) and START `2F4C` (claim 33, longest path 35). Fixed, whole table at three seeds 0 mismatching, gate 1,854 s, 731 tests. Commit `5190cc1`. Evidence `D:/f117-gate/p2-batch-bm2-accept/`, `bn-budget/`. |
| bn | 28 Oct | 1,496 | 2,067 named bytes | Nine routines from the proof agents: MPS_LOGO `0822` (huge-model memcpy) and `067C` (strstr), START `8F94` (divide-error handler, its own census row and PROGRAM_ENDS row), RSOUND.117 `0F98`, RSOUND.LOG `08BE` (625 of 626 instructions; the 3-byte jump at `0DAC` stays original), VGAME `D569`, ISOUND.LOG `0675` and `0468` (bounded prefixes), PLAYER `0211` (prefix to the STI). Four proofs carry private harness hunks that are now in `tests/func_lockstep.c`: a decline-floor check (0F98), a limit-probe return check (0468) and a return-code probe (D569), plus POPF_STOPS and PROGRAM_ENDS rows. Whole table at two seeds 0 mismatching, 60 routes identical with and without matched routines, gate 1,913 s, 740 tests, 287 mutations. Checklist round 5 adopted in the same commit (88 units, 3,641 bytes). Evidence `D:/f117-gate/p2-batch-bn-accept/`. |

Notes that still matter:

- Census boundaries can repeat a byte across two rows: batch af's 692 newly covered bytes gave 697
  census-byte credits (`D:/f117-gate/p2-batch-af-accept/credit-reconciliation.json`).
- END `3BF4`'s wide test envelope includes nine fixture callee JMPs: 95 covered
  instructions represent 86 original ones. Code-operand stores keep separate
  stop boundaries.
- Batches l-n replaced inherited opening coverage with fresh coverage of the
  complete body; no duplicate entry or credit was added. Switch-data bytes
  (`5963..5974`, `34D2..34E9`) and VGAME `11DFF` stay uncredited.

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
  This four-routine batch (c) also made the two runtime archives share one
  matched object. Full gate, all 45 N=1 routes and boot N=4/9 pass;
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
  (`0x441D`, in `3ADA`), so the fix never ran in the recompiled engine. A
  matched body with a fix site after its entry now declines while that fix is
  on (a `fix_inside` guard; 23 bodies in `src/matched/matched.c`, among them
  `3ADA` with D1 and D1REAL, flight input with D1REAL,
  `vgame_scene_set_override` with D34 and `vgame_detect_evaluate` with D6). No
  route triggers D34 or D6 yet.
- **Shared hooks.** `1450`'s address is also the presentation observer's
  frame-phase hook; it calls `observe_game_draw` itself and is on the
  `deliberate` duplicate list in `tests/test_overrides.c`.
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
- **Stale module instance (24 Oct; fixed in batch bd2).** A
  module's recomp instance stayed live after its memory was reused without
  another module load overlapping it, so matched overrides fired on whatever
  bytes were there: with ISOUND.LOG's `0603`-`0663` registered,
  `err_vgame_no_photo` first diverged at `169B:060D`. An override now runs
  only while its entry still holds the module's opcode byte, taken from the
  unpacked image where the translator has one, else from the loaded file
  (`src/recomp/recomp.c`; regression
  `tests/test_stale_override.c`). An earlier "region refusal at PIT wake
  boundaries" explanation was wrong: it reproduces with no translated module.
  Evidence `D:/f117-gate/p2-batch-bd-accept/flaky-parity-investigation.md`.
- **SETUP event-limit alert (14 Oct).** Not a game-code defect: two atomic
  native service clocks were counted as guest instructions; the ledger now
  tells them apart (`native_service_boundary`;
  [architecture.md](architecture.md#time)).

## Phase 3 - fixes and enhancements (switchable)

- [x] **Code overrides** attach fixes: hand-written C registered for a module
      address, pinned to the shipped file by hash. Normal play enables all
      34; core/headless and ordinary parity keep them off. `--fix none`
      restores the original and `--fix ID` adds selected fixes. Inert when off. Details:
      [architecture.md](architecture.md#the-run-time).
- [x] **Fixes for the original's bugs**: 34 normal-play switches, individually
      selectable; recordings retain the selected set. Each linked entry gives
      the implementation and its focused evidence. D10, D35 and D36 remain
      closed ([closed historical reports](bugs.md#closed-historical-reports)); `QUIZ` is a separate
      opt-in option.

| Fix | Verified behaviour |
|---|---|
| [D1](bugs.md#d1-the-frame-rate-controller-does-not-keep-world-time-at-real-time) | Frame cap on fast machines |
| [D1REAL](bugs.md#d1real-real-time-simulation-cadence) | Real-time simulation cadence (owner choice) |
| [D1TTL](bugs.md#d1ttl-long-lived-sa-5-missiles-cannot-cause-proximity-damage) | SA-5 proximity damage at high S |
| [D1SLOT](bugs.md#d1slot-cancelled-incoming-missiles-keep-their-launchers-slot-occupied) | Release cancelled missile slots |
| [D1PROX](bugs.md#d1prox-incoming-proximity-distance-shrinks-with-s) | Proximity distance across S (owner choice) |
| [D1ACCEL](bugs.md#d1accel-incoming-missile-acceleration-depends-on-s) | Missile acceleration across S |
| [D2](bugs.md#d2-adlib-digitised-speech-can-hang-the-game) | Scheduled AdLib speech |
| [D3](bugs.md#d3-keypad-keys-on-the-sound-driver-screen-quit-to-dos) | Keypad sound-board selection |
| [D4](bugs.md#d4-secret-airstrip-missions-are-disabled-outside-the-gulf) | Secret airstrips outside the Gulf |
| [D5](bugs.md#d5-supply-drops-never-award-credit) | Supply-drop credit |
| [D6](bugs.md#d6-stealth-mountains) | Zero detection cover (owner choice) |
| [D8](bugs.md#d8-laser-guided-bomb-pitch-clamp) | Laser-guided bomb pitch clamp; includes D7 |
| [D11](bugs.md#d11-fragile-saves) | Atomic save replacement |
| [D12](bugs.md#d12-ends-hidden-rating-tally-mixes-signed-and-unsigned) | Signed rating tally |
| [D34](bugs.md#d34-the-destroyed-object-table-has-no-end) | Bound destroyed-object table |
| [Q7](bugs.md#q7-zoomed-view-distance-uses-the-view-limit) | Zoomed-view range |
| [Q11](bugs.md#q11-far-model-positions-wrap-into-the-nearby-scene) | Full-width model-range cull |
| [Q12](bugs.md#q12-the-cannons-ground-test-reads-a-stale-stack-word) | Pass the cannon ground-test unit |
| [Q14](bugs.md#q14-an-overdue-objective-never-shows-time-expired) | Overdue objective text |
| [Q15](bugs.md#q15-adlib-noise-take-over-reads-the-wrong-generators-count) | AdLib noise take-over count |
| [Q16](bugs.md#q16-mt-32-volume-and-pan-modulation-overrun-the-data-range) | Clamp MT-32 volume and pan |
| [Q17](bugs.md#q17-pitch-ladder-labels-read-outside-their-table) | Pitch-ladder labels past 90 degrees |
| [Q18](bugs.md#q18-the-campaign-date-runs-past-december) | Campaign month/year rollover |
| [Q19](bugs.md#q19-the-enemy-troops-overlay-loses-its-place) | Enemy Troops overlay iteration |
| [Q20](bugs.md#q20-a-click-can-leave-the-briefing-popup-on-the-map) | Briefing popup ownership |
| [Q13a](bugs.md#q13a-a-sensorless-unit-reports-a-stale-range) | Sensorless range/bearing (owner choice) |
| [Q21](bugs.md#q21-the-steering-brackets-width-comes-from-stale-stack) | Retain steering-bracket width (owner choice) |
| [Q22](bugs.md#q22-the-armoury-shortage-counts-a-repeated-draw) | Do not count repeated armoury refusals |
| [Q4](bugs.md#q4-joystick-scaling-divides-by-an-unchecked-span) | Guard zero joystick spans (hardening) |
| [Q13b](bugs.md#q13b-the-written-orders-test-the-wrong-bases-record) | Read the landing-base record (hardening) |
| [D18](bugs.md#d18-an-unnamed-airbase-prints-null) | Empty unnamed-airbase name (hardening) |
| [Q23](bugs.md#q23-the-roster-fix-up-masks-one-record-ten-times) | Mask each roster record (hardening) |
| [D101](bugs.md#d101-transfer-tick-erase-reads-an-unwritten-slot) | Initialize the Transfer page tick slot and skip invalid erases |
| [Q12b](bugs.md#q12b-projectile-map-marker-size-comes-from-saved-si) | Explicit 2x2 projectile markers (99.956% nonzero in the route histogram) |

  - *Speed and cadence* ([speed-sweep.md](speed-sweep.md)): normal play runs
    at 60 million instructions/s with D1REAL's production cadence
    `--sim-hz 15` (15 normal / 30 compressed updates/s, S15, recorded cadence
    and explicit per-machine state; owner, 18 Oct), after the
    [broader 15/60 validation](speed-sweep.md#fifteen-step-play-mode-validation): twelve paired cases,
    960 exact airborne timing windows (160 unique minutes, 35 min 40 s
    actually compressed), a successful landing, a confirmed strike hit and
    credit, a paired thirty-minute compression soak and a native 4K60
    compressed-flight capture (p99 16.92 ms, all logic frames exact). At 40
    MIPS North Cape's compressed flight falls behind, hence 60. Unsuccessful
    automated air missions and the incomplete strike return remain explicit
    limits. No frame cap restores real-time pacing: D1's 11.6 fps cap gives a
    1.2882x mission clock
    ([D1 follow-up](speed-sweep.md#d1-follow-up-9-october-2026)) and a
    [15 fps cap](speed-sweep.md#actual-15-fps-limiter-experiment-9-october-2026)
    1.25x. Original 486/ET4000 references reach about 16.5 updates/s with S
    around 13: period-hardware responsiveness, not S15 intent.
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
    seven clock/dialog cases pass at 9, 20 and 40 MIPS in both engines and
    airborne clocks run at real time. The original's key-poll timing still
    limits exact whole-flight determinism across speeds.
- [x] **60+ fps and 4K presentation.** Optional and off by default
      (`--presentation enhanced`, or `--present replay|interp|rerun` with
      `--present-scale N`; `presentation =` / `present =` in f117a.ini). Design, measurements and history:
      [presentation.md](presentation.md). Rules: the original's logic tick and
      control timing are unchanged; its bitmap text, sprites and artwork are
      kept and integer-scaled; N=1 stays the exact original; an old path is
      retired only when its replacement passes N=1 equality. Target: 4K at
      60 Hz, and any scale and refresh rate, lower or higher. Higher
      resolution and higher frame rates are experimental and keep the
      original renderer's quirks (road and coastline jitter, which the
      original shows too; drawing-order mistakes; lines of one width at any
      distance).
  - *Done:* every mode, rerun included, keeps every N=1 route exact (every
    guest hash; rerun's own check at 45 routes, 15 Oct: 259,240 logic frames,
    259,166 in-between pairs; the gate's check over all 63 routes: 260,613 /
    260,527 at batch al); the
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
    at N=9 passed on 13 Oct ([results](presentation.md));
    hidden 4K throughput 119-150 fps. A palette frozen in threaded `interp`
    and `rerun` at N>1 (gun rounds solid red, fades frozen) was fixed on 15
    Oct. **Scene-input capture** (`F117R_OBSERVE_SCENE=N`,
    `tools/scene_capture.py`, `tools/scene_model.py`) is identical between
    engines and its decoded topology agrees with the original; the project's
    own scene renderer (`src/present/scene_mesh.c`) is an unconnected draft,
    no longer the main path.
  - *Owner verdict (15 Oct):* rerun's motion at 4K accepted "at least for
    now" (fullscreen, apart from the original's jitter).
  - *Closed (18-19 Oct):* fullscreen 4K pacing (sorted override dispatch and
    bulk observer row snapshots, `D:/f117-gate/4k60-fix-20261018/`; a 24-minute
    fullscreen landing flight at 15 Hz / 60 MIPS: p99 16.95 ms, zero missed,
    every logic frame exact); map-edge specks from fine line caps
    (`D:/f117-gate/map-specks-20261018/`); the `DF03`/`C2C8` vertex group; the
    playback-clock default 0.4 / 0.025; long flights and constrained cores as a
    labelled proxy for slower machines. Evidence and figures:
    [presentation.md](presentation.md) and the Stage 3/4 entries in
    `docs/progress.json`. Other physical machines, other refresh rates and the
    owner's visual judgement are a Phase 4 maybe; what the owner decided
    against is under "Possible future work" below.
- [ ] **Re-release features** (owner, 24 Oct 2026: what a highly praised re-release offers; scored in P3 at weight 31, nothing credited yet).
      *Built so far* (24 Oct, `ab4256b`): `--presentation original|enhanced` and `--fixes original|recommended`, also as
      `presentation =` and `fixes =` in f117a.ini (`remastered` is refused as not built; later `--fix` options make a custom set);
      saves tagged with the fix set that last wrote them (a `.fixset` file beside the save folder, never in it), warning once when
      a session crosses between the original game and a fixed set. The picture default is still Original. Design notes, not built:
      [setup-autoanswer.md](setup-autoanswer.md) (SETUP's answers from the settings) and
      [sound-board.md](sound-board.md) (every sound through each driver, with the Macintosh recordings as a fourth
      column, deciding hybrid sound's split).
      *Two settings instead of preset modes.* **Presentation:** Original (320x200, original rate, nearest-neighbour 4:3, the validated
      default picture), Enhanced (the finer 4:3 redraw with in-between frames at 60+ fps; the final linear stretch is a setting with a
      sharp option; the original's road and coastline jitter stays) or Remastered (below). **Fixes:** Original (473.04, every fix off),
      Recommended (every fix: the final patch, the fan-community patches, credited, and OpenNighthawk's own) or Custom. Any presentation
      combines with any fix set; the shipped default is Enhanced with Recommended. Everything but Remastered stays 4:3.
      *Remastered:* widescreen with a widened field of view, a longer draw distance, depth-correct drawing (the original sorts whole
      objects, so a far hangar's edge can be drawn over a nearer hangar until the camera is closer; owner's screenshots, 24 Oct) and no
      road or coastline jitter.
      *Also:* an in-game settings menu for every option now set on the command line or in an .ini, which also answers SETUP's
      joystick and sound-driver questions as typed keys (from the controller and sound-device settings; an option still shows them); controller and HOTAS support with
      remapping, an on-screen key reference and mouse where the original had it; the sound device (AdLib, Roland MT-32 via Munt, PC
      speaker, or hybrid: MT-32 music with AdLib sound effects, owner 24 Oct; the game keeps the Roland driver and a hidden
      AdLib driver on a machine copy receives the same sound requests, so only its effects are heard) chosen in the menu; an optional CRT filter for the 4:3 presentations; flight replays (save a sortie and watch it back,
      other cameras in Remastered); accessibility (subtitles for the speech, full remapping, a colourblind-safe HUD in Remastered).
      *Rules:* presentation never feeds the simulation, proved by equal simulation state across the three presentations on the same
      inputs; the mission clock stays real; saves carry their fix set and warn across the bug boundary. Conveniences that change play
      (save anywhere, difficulty) are not planned; if added they sit apart from both settings, off by default.

## Possible future work (decided against for now, not scored)

Things the owner decided not to do for the Original and Enhanced presentations (the original renderer). They are not scored by `tools/progress.py`
(owner, 17 Oct 2026) and stay here as ideas that may be taken up later, as with
Phase 4. The decisions and their reasons are in
[presentation.md](presentation.md) ("Stop improving primitive pairing", "The
jitter is the original's; no smoothing", "Do not extend the draw distance").

- [ ] **Smooth the original's road and coastline jitter** in the original renderer, at N=1 or any higher
      scale (owner, 15 Oct 2026: it is the original's own, shown by the GOG
      DOSBox release too; N=1 stays authentic and the finer modes keep the
      original renderer's quirks). A jitter-free picture is planned for Remastered (Phase 3, 24 Oct 2026).
- [ ] **Refine clipped or off-screen outlines**: 6.1% of outline strokes in the
      owner's sharp flight are still drawn from scaled whole-pixel endpoints.
- [ ] **More work on primitive pairing** for in-between pictures (owner, 15 Oct
      2026: stopped; identity pairing stays opt-in, `F117_PRESENT_IDENT=1`,
      because it deformed objects; in-between pictures come from `rerun`).
- [ ] **A longer draw distance** in the original renderer (owner, 15 Oct 2026). Planned for Remastered instead (Phase 3, 24 Oct 2026).

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
      shows in the GOG DOSBox release too). Not planned for Original or Enhanced;
      Remastered's new renderer replaces it (Phase 3).

- [ ] **Maybe: line widths by drawing role, and a physical-panel review of the
      finer modes.** The remaining open parts of the high-resolution re-draw
      (Stage 2). One perpendicular-width pen serves HUD, outlines and strips
      today; widths by reliable drawing role (HUD tape, outline, strip) are a
      tuning task, not a fixed class list, and the look on a real screen is a
      person's judgement. Moved here on 18 Oct 2026 (owner): not scored, taken
      up only if wanted.
- [ ] **Maybe: the hardware-only parts of Stages 3 and 4** (owner, 18 Oct 2026).
      Pacing and smoothness on other physical machines (slower or older CPUs,
      weaker GPUs), on displays with other refresh rates, and the owner's own
      judgement of how motion looks on a real screen (tearing, judder). They
      need hardware or a person this project does not have; the testable parts
      stay in Stages 3 and 4 and are measured here (long flights, constrained
      cores as a labelled proxy, playback-clock tuning). Not scored.
- [ ] **Maybe: Macintosh-version improvements in OpenNighthawk** (owner, 24 Oct 2026).
      Anything the 1994 Mac port adds that is worth offering, read at run time from the player's own Mac
      copy and never shipped: its 640x480 and 512x384 art (cockpit, maps, screens), its scrolled-tape HUD
      as a HUD style, its digitised effects, music and word-by-word speech (beyond the planned sound-board
      column), and its other presentation changes. The game's logic stays DOS's in every mode; Mac-only
      behaviour is a switchable option at most. What is known: [other-versions.md](other-versions.md).
      An oracle to verify Mac behaviour, if one is needed, would start from Snow (open source, MIT,
      emulates the Mac II with its colour cards) with MAME as a second reference; it needs the owner's
      Mac II ROM. Not scored.

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
