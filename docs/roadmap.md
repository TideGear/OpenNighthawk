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

- **Translation.** All 17 code files and the LZEXE decompressor: 89,369
  instruction starts, 96% of the code-area bytes (the rest is strings, tables
  and variables; `tools/census.py`). Changed code and declined override
  entries fall back to the interpreter.
- **Engine parity.** 63 scripted routes (62 sessions) are identical between
  the interpreter and the recompiled code at every 50-million-clock checkpoint
  and at the end; every translated instruction is held to the interpreter from
  random states (5,723,136 comparisons under each of the two timing profiles,
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
- **The gate** (`tools/build_recomp.py`): translate, build, unit tests (427),
  coverage, both engines on every route, instruction lockstep under both
  timing profiles, matched-routine lockstep at three seeds, and the
  routes-only and event-limit checks. About 26-30 minutes with every
  interpreter route verified fresh (batch al: 1,579 s); costs and what was
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
      **Now (21 Oct):** 1,189 addresses matched. The score covers the expanded
      game-code scope (all 17 files): the Reimp census rows plus code outside them that routes
      or proofs execute and reviewed static code (`docs/census-code.tsv`, 154 ranges):
      **165,256 / 204,747 creditable bytes, P2 81.68%, overall 94.41%**. In the census
      alone, named code runs 159,769 of 165,265 creditable bytes (1,108 rows wholly,
      294 in part). Credit is by byte since 16 October; data, code that stays original
      and certified-dead code leave the denominator
      ([progress.md](progress.md#what-is-measured)). Per-batch credit is in the table below.
      **What is left (39,491 bytes; `tools/census_classify.py`):**
      - About 15,150 outside the census rows: 13,351 reviewed static code bytes that no
        route runs, and about 1,800 executed bytes no routine names. A route plan
        (`D:/f117-gate/static-route-plan-20261021/`, 21 Oct) identified no additional route.
        The resumed independent review retains every range: indirect/callback/vector and writer
        closure is incomplete, and the earlier constant-M argument overlooked M/E pixel helpers
        inside a purported CGA/Tandy range. No new dead-code exclusion is adopted. Details:
        [static-code-review.md](static-code-review.md). DSWAP's identified formatting callers need
        declined floppy support or machine-option-only failures.
      - 5,496 inside the census rows: 947 that routes run but that stay original or were
        declined (below); 4,322 that no route runs (about 2,500 error paths only a
        test-only machine option could reach, which the owner decided not to build;
        831 probable-dead bytes that need evidence; the rest needs combat or very long
        careers); 222 unreached bytes held back on weak evidence; one live jump slot.
      - 18,835 validated executed bytes in the launcher, resident graphics library and sound
        drivers, now included (21 Oct scope expansion; no matching credit lost). The remaining
        about 654 held static-discovery bytes (42 groups) are still outside the score.
      **Owner decision (21 Oct): no meaningful game code may be missed.** Approved: bring
      the launcher, graphics library and sound drivers into the scope (the percentage
      drops at first; measurement and scoring steps 0/1a/1b done in `D:/f117-gate/modules-scope-20261021/`;
      harness and proof-kit work remains); the static-code work above; a review of the unreviewed bytes; and a
      reference entry (name and one-line purpose) for every range that cannot be matched,
      so nothing is left unexplained even when it earns no credit.
      **Dead code** leaves the denominator only when certain (owner, 19 Oct), with a
      certificate per row: `docs/census-dead.tsv` (73 rows, 2,927 bytes) and
      `docs/census-dead-ranges.tsv` (868 bytes of jump slots no program binds or names
      and 367 bytes of code no walk decodes; 21 Oct reviews). The START quiz is an option
      (`--fix QUIZ`), not dead code, and its routines are matched.
      **Stays original code:** the timer interrupts (VGAME `1C9B`, START `8D0B`, END
      `4BEF`) open with STI, after which the run loop admits one more instruction
      before it looks at events, so a matched body could do nothing the original does
      not. The C start-up (START `8EDC` and its copies VGAME `E6BE`, DSWAP `0C7E`, SETUP
      `1516`, END `4DD2`, PLAYER `1096`, MPS_LOGO `0146:0016`) stops after its own STI and
      never returns, so no lockstep state can complete it. The palette loaders with STI
      inside their colour loop (VGAME `14C2:0000`, START `8378`, END `42E4`). The line
      clipper's divide-error handler (`0FB2:04DD`) is fixture-proven but no input reaches
      it (146,153 real clip divisions across 14 routes stay far under its threshold).
      Declined: VGAME `0010` (`main`), START `8427` and END `4393` (wait loops routes
      cannot finish), END `57B0` (an interior arm of printf), MPS_LOGO `1ADC` and `1C82`
      (unequal); the list is `D:/f117-gate/kit/DECLINED.tsv`.

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

### Batches e to al (16-21 October 2026)

Every batch below passed the same proof (see above): five seeds per routine
with every instruction reached, all mutations caught, exact boot (26
checkpoints) and real-route proofs, optimized seed runs, then a fresh full gate
(about 22-32 minutes), the N=1 presentation check over every route (45 routes then, 63 now) and boot N=4/9.
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
| t | 18 Oct | 1,015 | 962 | VGAME `0FB2:0012` low-detail sky horizon line (164 instructions; the census's `0000:FB32` is the same address, and only `0FB2:0012` is ever entered); VGAME `CE65` key-arm selection (the jump table and key chain of the flight key dispatcher; it hands back to the original arm, so only the chain counts) and `CD7E` panel-state cycle (proved on `strike_training`). The 58 `FATAL_JUMPS` hand-back rows and a `SHARED_ENDINGS` row are in `tests/func_lockstep.c`. Batch s (`0FB2:0012` alone) failed step 7c at a rotating seed (see the handoff's open harness issue) and was folded into t |
| u | 19 Oct | 1,016 | 4,552 | VGAME `9EFA` `vgame_display_pass`, the flight display pass (1,628 instructions): weapon markers, gun tracers and their hit tests, the impact blast, the weapon locks, the target and reconnaissance cameras, bomb symbology and the supply-drop radio beacons. Written as named sections (a first mechanical transcript was rejected). Also in this batch: the `display_tour` route (46 routes now), and a harness fix for returns reached through misaligned fuzzed code (CTest `matched_projectile_misaligned_return`) |
| v | 19 Oct | 1,018 | 873 | VGAME `8F71` the navigation display (legs, times and clock bar; 258 instructions) and VGAME `93E9` the armament panel refresh (75): both reached only by the new `display_tour` route (51 checkpoints; 15 and 1 calls). `8F71` misses two instructions there (damage bit 5 of `[3664]`), covered by the fixture |
| w | 19 Oct | 1,019 | 784 | START `1C2F` `start_site_popup_text`: the info popup text of a briefing-map airbase or radar site (303 instructions; two-slot loop; proved on `display_tour`, 51 checkpoints / 40 calls; the route misses 23 instructions that need flagged sites in a theatre briefing, covered by the fixture) |
| x | 19 Oct | 1,020 | 301 | START `11DB` `start_map_hover_light`: the briefing-map hover handler (127 instructions, 301 bytes: relights the palette entry of the item under the pointer, the help line and the site popup; the census rows `11DB`, `1285` and `12DB` are parts of it). Proved on `display_tour` (1,301 calls) |
| aa | 19 Oct | 1,083 | 2,215 | 63 routines on the new routes: the flight key dispatcher's arms (VGAME `C8CA`..`D3F8`: 55 arms, each a hand-back routine named by its key: zoom, countermeasures, gear, flaps, HUD options, the camera and view keys, the height hold, the sensitivity, detail, sound and frame-rate steps, the training switch, the waypoint reset, the display selectors F3-F10, Enter/Backspace buttons and more), the eject arm `CD04`, START's Alt+Q panel `7611` and int86 `9520`, END's Alt+Q panel `3E61`, TASS page `3BBF`, int86 `519E` and the whole icon-frame switch `0977`. On `key_tour`, `quit_dialogs`, `ejection_tass`, `display_tour`, `navigation_grid`, `recon`, `airair_type5`, `korea_strike` and `camera_gradients`. Mutation counts: 1,709 in all, all caught; 13 arms of two or three instructions have fewer than 20 (5 to 18) because no more distinct edits exist |
| ab | 19 Oct | 1,120 | 2,040 | 37 small routines on existing routes, found by four module-group proof agents: SETUP (cursor, shut-down, tick wait, timer-hook install), VGAME (display-page registration, background span fill, picture close/read, frame loop), END (file reads, allocate-or-quit, debrief picture, DOS open, exit), START (hangar scene, the six objective-sentence arms, file loads, buffer allocation, clock hook, long-to-text, malloc), MPS_LOGO and PLAYER (palette write/read, allocators, break-vector hook, timer install, clock-hook step). Also the playback-clock default 0.4 / 0.025. Dropped: END `4160` (diverged on a real route), `23D6` and `3682` (they replace existing fragment rows), VGAME `0010`, `12A11`, END `03D9`, `57B0`, MPS_LOGO `067C`. 1,029 mutations, all caught |
| ac | 20 Oct | 1,125 | 290 | 5 routines: END `4160` (DOS block read: its first proof hard-coded END's data segment, which the loader relocates, and failed on `landing`; fixed by reading the immediate from the code), END `2430` and `36D6` (the tails of the ribbon and death-message routines, registered beside their existing fragment rows), MPS_LOGO `0030` (`fread` of bytes into a far buffer) and PLAYER `01F6` (`player_finish`). 127 mutations, all caught. Also: `/Os` on `matched.c` (`/O2` took 90+ minutes to compile it from batch ab on), and 3,299 bytes certified dead left the denominator (`docs/census-dead.tsv`) |
| ad | 20 Oct | 1,125 | 0 (routes) | seven new routes, no new routine: five damaged-install routes (`err_player_assert`, `err_player_divide`, `err_boot_series_palette`, `err_driver_overlay`, `err_dswap_nostart`: the original programs' own error handlers run against a private copy of the install with one file removed or damaged, `tools/route_damage.py`) and two compressed-boot flights (`libya_tour_eject`, `korea_release_eject`). 56 routes / 55 sessions identical between the engines. They run about 3,250 more census bytes (128 rows) that no routine covers yet. Gate 2,184 s (the compile took 5 minutes with `/Os`) |
| ae | 20 Oct | 1,125 | 0 (option) | the START quiz as an option: `--fix QUIZ` (an override at START `001D` that pushes `0020` and runs the quiz routine `0703`, the call GOG patched to `90 90 90`; off by default, `all` and normal play never switch it on) and the routes `quiz_right` / `quiz_wrong` (both verdict arms, `planes2.pic` milestone). The 633 quiz bytes left `docs/census-dead.tsv` and are back in the denominator, so P2 dipped from 88.38% to 88.09%. 58 routes / 57 sessions identical; gate 2,305 s |
| af | 20 Oct | 1,148 | 697 census credits | 23 routines: DSWAP `0647`, `0668`, `06EA`, `08F4`, `10F4` (close/read, picture block/rows, BIOS disk); END `00C7`, `00C8`, `22EA`, `47FE`, `2BCB`, `42BE`, `451B` (empty hooks, free-or-quit, palette, mouse); PLAYER `1C12`, `1D8D`, `1F54` (string/integer formatting and abort); SETUP `1509` (DOS string); START `057B`, `057C`, `885E` and VGAME `11D8:0155`, `02C2`, `02C3` (empty hooks); VGAME `1BC2` (flight-loop wrapper). 363 fully covered instructions, 550 caught mutations, 115 optimized seed checks; 385 CTests and all 58 routes pass. Gate 1,555 s; acceptance 1,689 s with presentation overlap. Serial/parallel translation is byte-identical; coverage loading 51.23 to 5.20 s. The harness span alias and the ROM-free scheduling test's install dependency are repaired. |
| ag | 20 Oct | 1,162 | 645 named bytes | 14 routines from the rows that routes run but no routine covered: MPS_LOGO `0045:003A` (string position), `0045:0102` (DOS block free), `0146:0FE4` (formatter scan), `0146:15FA` (`_write`, with its shared tails), `0146:17B0` (checked allocation), `0146:1A64` (far jump to the allocator); PLAYER `1FD0` (the C run-time's stack check), `22AF` (raise()'s exit(3) tail); END `5948` (`%d` arm), `3692` (debrief message arm); DSWAP `0599` (DOS open or report), `0848` (colour registers); VGAME `104E:0082` (far sine entry), `1058:000E` (far HUD colour setup). 313 fully covered instructions, 341 caught mutations, 70 optimized seed checks; 399 CTests, 58 identical routes / 57 fresh interpreter sessions, both timing profiles, three full matched seeds and event limits pass. Gate 1,604 s, acceptance 1,731 s with presentation overlap. Five more proofs are not integrated: MPS_LOGO `27C2` and `163A` (their real-route proof found no call on the routes tried), VGAME `14C2:00B7`, `1058:000A` and `1058:0012` (the lockstep span table is keyed by IP, so the new rows would alias same-IP routines in other segments: they need a segment key first). Blocked as not routines or already matched: PLAYER `1BBA`, `1D50`, `1C3D`, `1D02` (entries inside the printf dispatcher `1BB6`, whose own proof has no verdict), `222D` (a switch table), `1FCF` (padding), END `4FA4` (run inline by `end_exit`), VGAME `11322`, `11372`, `11ED5` (already matched under other entries), MPS_LOGO `039A` (already matched). Hand-back routines `2444`, `2c10` and the formatter and runtime bodies whose block macros the private audit cannot enumerate are marked partial for that audit only; credit is by byte coverage. |
| ah | 20 Oct | 1,168 | 787 named bytes | 6 routines: the START quiz (`057E` draws the screen, `0703` takes the pick and judges it, `0786` highlights the hovered button; the census rows `0583`, `06E0`, `072E` and `0788` are mid-instruction pieces of these three, credited by byte; proved on `quiz_right` with `--fix QUIZ`), PLAYER `1BB6` (printf's conversion dispatch, 471 bytes, 65 instructions; seeds now compare 6,124-6,146 states each, the earlier proof's seeds had skipped nearly all; it also credits the census rows `1BBA` and `1D50`, whose first bytes are mid-instruction), VGAME `1058:000A` and `1058:0012` (far entries of the HUD pass and layout switch). 328 fully covered instructions, 275 caught mutations, 30 optimized seed checks; 405 CTests, 58 identical routes / 57 fresh interpreter sessions, both timing profiles, three full matched seeds and event limits pass. Gate 1,783 s, acceptance 1,729 s. The lockstep span table gained a segment-keyed companion (`SPANS_SEG`) because `1058:000A` and `1058:0012` share addresses with routines in other segments. VGAME `14C2:00B7` was proved too but is the same code as the existing `1452:07B7` (linear 14CD7), so it was dropped; a first gate run caught the duplicate through its routine tally. Three claimed-but-uncredited bytes come from `1BB6`'s shared tail at `1D52`, entered from two arms. |
| ai | 20 Oct | 1,168 | 41 census bytes (routes) | five new damaged-install routes, no new routine: `err_start_no_world` (LB.WLD removed: START's run-time error banner), `err_start_terrain_files` (LB.3DG removed, LB.3DT magic zeroed), `err_vgame_no_photo` (PHOTO.3DX removed: VGAME's file-error arm, a key already pressed so the matched `getch` never waits), `err_dswap_no_swap_palette` (LABSLOGO.SS, START.EXE and SWAP.PAL removed: MPS_LOGO's not-found arm and DSWAP's fatal-text printer) and `err_end_no_tass` (TASS.PIC removed after the ejection flow); 63 routes / 62 sessions identical between the engines; together they run 581 never-run census code bytes that no routine covers yet. Also: VGAME `D9E7` (set the scene origin) read its three arguments before its stores, but the original reads each just before its store: the commit-keyed seed of step 7c found it when DS and SS overlap (CTest `matched_scene_origin_stack_alias`); the dead-list row VGAME `0EBEA` (9 bytes) was removed because a route reaches it; `tools/presentation_check.py` names the two routes that start VGAME but end at a file error as `NO_FLIGHT_ROUTES`. Four gate attempts: one 7c mismatch (`D9E7`), one event-limit overrun (an open `getch` wait: a key typed after 1.4M clocks waits, before 1.0M does not), one presentation pairing requirement, then pass. Gate and acceptance 1,658 s, 406 CTests. |
| aj | 20 Oct | 1,172 | 277 named bytes | 4 routines on the new damaged-install routes: DSWAP `0754` (the fatal-error text tail: clear the screen, print the file name and message, end the program), MPS_LOGO `0146:1364` (the formatter's field output: sign, padding and text; the census row `27C2`/`1362` is two bytes of jump in front of it that no route takes), VGAME `0CDD` (show a message line and wait for a key) and END `41CA` (the fatal-error screen; the census row `419A` is a mid-instruction piece beside it). 116 fully covered instructions, 122 caught mutations, 20 optimized seed checks; 410 CTests, 63 routes / 62 sessions identical, three full matched seeds clean. Gate 1,770 s, acceptance 1,753 s with presentation overlap (a keyed baseline for 63 routes now exists). First attempt failed at the commit-keyed seed: the START quiz judge's fixture matched the pick to the answer only by chance, so one seed never ran the right-answer arm; it now forces both verdicts in every fourth state each. Of about 25 rows given to proof agents from the new routes, 21 were already matched under other names or are not routines (START `09800` `09848` `09873` `07D71` `07E88` `0963E`, MPS_LOGO `007E2` `02CF4` `021D2` `0171A`, VGAME `0F5F2` `0895E` `009A0` `0E586` `0F1E0`, END `0400F`, and shared DOS tails). |
| ak | 21 Oct | 1,181 | 875 named bytes | 9 routines in rows that routes run but no routine covered: DSWAP `023E` (draw the disk-swap screen), `03A7` (open a picture file, decode it onto a page, close it), `0AD5` (the picture decoder's start) and `0BCF` (its code and table step: it leaves by `JMP BP`, handled by a `run_to_ret` rule and the pop-distance exemption); MPS_LOGO `0006:0044` (clear the text screen), `0006:000C` (`fatal_error`) and `0146:01D3` (the whole `exit()` entry through the shared tail; `01DA`/`01E2` are entered by no route, so the earlier `_exit` proof stays private); END `3B9A` (put up the report picture) and `03D9` (debriefing menu, rating and tally: D12 has two fix sites inside, so the body declines while D12 is on, like five other bodies). 379 fully covered instructions, 283 caught mutations, 45 optimized seed checks; 419 CTests, 63 routes / 62 sessions identical, three full matched seeds clean. Gate 1,719 s, acceptance 1,606 s with presentation overlap. Fixtures force each branch outcome in a fixed share of states and were run at six extra seeds. The private audit needed two allowances: bodies without block macros take the placeholder prefix, and a hook inside the routine is accepted when a covering `fix_inside` guard declines it. |
| al | 21 Oct | 1,189 | 495 named bytes | 8 PLAYER routines in rows that routes run but no routine covered: `1A7E` (printf's format scan, with the stack check inlined), `18C4` (`fprintf`), `1476` (assert failure), `1839` (write a message to the console), `15BA` (start-up check failed), `223A` (`raise`: it hands its tail to the already matched `22AF`, so no double credit), `026B:0244` (program the timer chip; a segment-keyed span and a `POPF_STOPS` row because `0000:0244` is another routine) and `0010` (`main`; the dossier span was one byte short of its RET, which had skipped every state). 228 fully covered instructions, 255 caught mutations, 40 optimized seed checks; 427 CTests, 63 routes / 62 sessions identical, three full matched seeds clean. Gate 1,579 s, acceptance 1,549 s with overlap. Four claimed-but-uncredited bytes (inlined stack check). The batch copy of the private audit accepts a call inside a block for a `partial` body (the lockstep compares the clocks). |

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
  - *Speed* ([speed-sweep.md](speed-sweep.md)): normal play now defaults to
    60 million instructions/s and D1REAL at 15 normal /30 compressed updates/s,
    approved after [broader validation](cadence15-validation.md). The historical
    20-million setting without repairs was selected
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
    A further private 15 Hz/S15 normal, 30 Hz/S15 compressed candidate passes
    clock/dialog and rapid-transition checks at 40 MIPS after deadline phase
    correction. Production `--sim-hz 15` is now implemented with recorded
    cadence and explicit per-machine state; 239 CTests and all-fixes clock/
    transition checks pass. Resumed 18 Oct full gate passes (45 routes,
    both instruction profiles and three matched-routine seeds). Twelve fresh
    Korea/Kuwait/North Cape flights agree between engines, but North Cape
    compressed flight falls behind at 40 MIPS while airborne and undamaged
    (272/300 updates, 18/20 mission seconds in ten seconds). A private 60-MIPS
    reserve comparison keeps the same update cap: both engines pass ten
    compressed minutes and all clock/transitions at 60 MIPS. The 40-MIPS
    hidden app/replay and full presentation checks pass. Four 60-MIPS flight
    pairs agree exactly with all airborne windows passing (31.8 observed
    minutes, 73 enemy launches); full North Cape replay keeps 6,681 frames
    exact and matches the API. The first native 4K/60 pursuit trace fails pacing
    despite exact guest state (p99 32.93 ms, 12.95% over 25 ms, one startup
    interpolation rewind). The later dispatch, row-capture and startup-clock
    repairs pass that same recording at p99 16.94 ms with exact state.
    [Broader 15/60 validation](cadence15-validation.md) adds twelve paired
    cases, 960 exact airborne timing windows (160 unique minutes, including
    35 min 40 s actually compressed), a successful landing, a confirmed
    strike hit/credit and a paired thirty-minute compression soak. The
    dedicated exposure flights record 183 enemy launches. Unsuccessful
    automated air missions and the incomplete strike return remain explicit
    limits. The separate native 4K60 compressed-flight capture also passes:
    21,565 pictures, p99 16.92 ms, zero intervals over 25 ms, 21.84 ms
    drift and all 8,992 logic frames/final state exact. The technical
    recommendation is now 15/60 for normal play; after validation, the owner
    approved making it the application default. Original 486/ET4000
    parked references reach about 16.5 actual updates/s with S around 13,
    supporting period-hardware responsiveness without establishing S15 intent.
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
    passed the earlier short checks. The 18 Oct 15-Hz /60-MIPS type-5 pursuit
    capture also fails (18,606 flight pictures, p99 32.93 ms, 12.95% over
    25 ms and one startup interpolation rewind; exact guest state, 5.2 ms
    drift). Its scene differs, so cadence causality is untested. Screenshot
    capture timed out twice; visual assessment remains open. See
    presentation.md for evidence;
    hidden profiling locates copied-draw CPU cost (46% main-thread samples
    in linear override dispatch). The 18 Oct repair replaces those scans with
    a sorted address lookup and keeps startup playback on one clock; fresh
    full gate (241 CTests), all-route rerun and the same-record extra-draw
    checks pass under `D:/f117-gate/4k60-fix-20261018/`. Fullscreen improves
    to p99 21.58 ms, 0.03291% over 25 ms and no rewind, with exact guest
    state, but still misses the 20.83-ms p99 limit. The observer follow-up
    uses wrap-safe bulk row snapshots and skips unchanged pixel blocks.
    Fresh full gate (242 CTests), all-route rerun, N=4/9 boot and combat
    copy-isolation checks pass. The same-record fullscreen capture now
    passes: 21,330 flight pictures, p99 16.94 ms, zero intervals over 25 ms,
    no rewind, 15.94 ms clock drift and all 5,396 frames/final state exact.
    Evidence is under `4k60-fix-20261018/observer/`. Visual assessment and
    all-scene acceptance remained open at that checkpoint; broader validation
    subsequently passed and the owner approved 15 Hz /60 MIPS as default. The owner
    watched part of a later replay and found flight fine apart from map-edge
    specks. Fine integer line caps were escaping their endpoint cells; the
    clipping repair removes the reproduced remnants (22/32/38 pixels to zero)
    with all 953 captured logic frames and the interpreter reference exact.
    Evidence is under `D:/f117-gate/map-specks-20261018/`;
    weapon and target symbology; long
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
