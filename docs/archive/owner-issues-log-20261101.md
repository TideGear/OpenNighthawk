# Owner issues: the 1 November log (archived)

> **Archived on 5 November 2026** from [../owner-issues.md](../owner-issues.md), unchanged: the pictures-per-second measurement and the six full-gate records of 1 November. The owner decisions in it are carried in
> that file's "Standing notes from the owner".

## Pictures per second (1 November 2026, 13:30)

Measured with `--present-log` on the owner's takeoff roll (clock 7.0 G to 7.3 G, 1920 x 1080, `--present-scale 3`, one run at a time): Enhanced about 56 pictures a second (it is held to the display's rate), Remastered 40.6 before this change and 44.6 after (the shaded fill that draws the sky and ground bands now steps its weights along a row instead of dividing at every pixel: the bands went from 5.5 ms to 3.8 ms a picture, the picture itself is unchanged, 0 differing pixels outside the lights of the R12 fix). A Remastered picture still costs about 9.5 ms to compose plus about 6 ms of other work in the loop plus 6 ms to present, against 16.7 ms for 60 a second. The next step is to compose on a second thread while the loop presents the picture before it (needs the picture, the palette and the frame's primitives copied for the worker); not started.

**Update, 1 November 2026, 13:00:** the composite now shares a picture's rows among four threads (the caller and three helpers; `remaster_live_set_parallel`, `F117_REMASTER_THREADS_OFF=1` turns it off). The scene is drawn in three stages with a barrier between them (the lights' depth test reads the rows above and below). Proof that it is the same picture: `F117_REMASTER_THREADCHECK=1` redraws every picture on one thread and compares every output pixel; over the whole takeoff roll (1,681 pictures at 1920 x 1080) 0 differ; `tests/test_remaster_raster.c` case 4 does the same for the raster. Pictures a second on the roll, one run at a time: 48.1 on one thread, 60.2 on four (the display's rate). Two things found on the way: the cache of nearest palette entries was keyed by the palette's address, so it could answer from an older palette (now keyed by the palette's contents; only the palette index of a replaced pixel could differ, never its colour), and the ordinary line pass inherited the lights' slope allowance by accident (now cleared). Not yet seen by the owner.

**Owner decisions, 1 Nov 22:45:** the 22 repairs that reach no route or flight we have stay on by default (not made opt-in); no further full gate tonight; Esc stays the game's, the menu opens on tilde; the Mac intro has no recording (the DOS music plays); the hose-disconnect sound is not in the original (S8 withdrawn); Q11 retired.

**`ONTests` at 23:50 (1 Nov):** built from the clean P3 branch (the P3 commits only, up to the handoff `1271620`, without the P2 session's staged `matched.c`); 832 of 832 unit tests; not full-gated (the owner's limit). It has T1 (tilde menu everywhere), R15 (the menu shows what is running), R16 (hangars and aircraft stay until they leave the window), the Esc and tilde rules, and everything of the 22:23 gate. Open for the owner's look: R16, T1, R15; still open: R17, R18, R19, R9.

## Full gate on my commits, the last one of 1 November (22:23)

`tools/build_recomp.py` on a branch of only the P3 commits (`p3-gate`, from `fed0186`, without the P2 session's staged `5cd50d8`, which makes the `code_overrides` test fail: `docs/archive/p2-brief-ci-failure.md`) up to the Esc menu: exit 0. 832 of 832 unit tests; 5,764,032 instruction states compared across all modules, 0 mismatching; 1,646 matched routines, about 6.9 M states at each of three seeds, 0 mismatching; no routine ran past its event limit. Log: `D:/f117-gate/gate-master11-gate-20261101/`. It covers R7 to R15, S7, S9 (the sound driver's speech wait loop as one step), the Remastered margins, the menu rows, the CRT mask and glow, `verify_flights`. The last host-only commits (the tilde menu, Esc left to the game) were not gated, by the owner's limit of one gate for the night: the same tree with them passes all 832 unit tests, and `ONTests` has that build (22:26). Owner: no further full gate tonight unless approved.

## Full gate on master, with the wide margins (1 November 2026, 20:46)

`tools/build_recomp.py` on `master` at `28728bf` (the rerun copy's widened cull for the Remastered margins, on top of the Q11 retirement and the host features): exit 0. 832 of 832 unit tests; 5,764,032 instruction states compared across all modules, 0 mismatching; 1,646 matched routines, about 6.9 M states at each of three seeds, 0 mismatching; no routine ran past its event limit. `Desktop\ONTests
117a.exe` is this build. Log: `D:/f117-gate/gate-master9-gate-20261101/`.

## Full gate on master, later still (1 November 2026, 20:02)

`tools/build_recomp.py` on `master` at `1ce986f` (Q11 retired, the replay-fixes option, flight replays' typed jump, controller keys, the new menu rows, the CRT mask and glow, `tools/verify_flights.py`): exit 0. 832 of 832 unit tests; 5,764,032 instruction states compared across all modules, 0 mismatching; 1,646 matched routines, about 6.9 M states at each of three seeds, 0 mismatching; no routine ran past its event limit. `tools/verify_flights.py` on the owner's two takeoffs: placed objects 11 and aircraft 11 as GOG's DOSBox shows, and the fixes that change each flight are the reviewed lists. Log: `D:/f117-gate/gate-master8-gate-20261101/`.

## Full gate on master, with R10 (1 November 2026, 17:54)

`tools/build_recomp.py` on `master` at `6cb6b49` (R10: the DOS-poll stub's `LOOP $` as one step in `machine_run`, with R7, R13 and the fix tools): exit 0. 832 of 832 unit tests; 5,764,032 instruction states compared across all modules, 0 mismatching; 1,646 matched routines, about 6.9 M states at each of three seeds, 0 mismatching; no routine ran past its event limit. Log: `D:/f117-gate/gate-master5-gate-20261101/`.

## Full gate on master, later (1 November 2026, 17:09)

`tools/build_recomp.py` on `master` at `ec2c734` (R7: Q11 opt-in; R13: kept map rectangles survive a reset; `--replay-fixes`; the fix audit): exit 0 in about 2,170 seconds. 832 of 832 unit tests; 1,646 matched routines, about 6.9 M states compared at each of the three seeds, 0 mismatching; every routes-only routine ran on a route; none ran past its event limit. Log: `D:/f117-gate/gate-master4-gate-20261101/`.

## Full gate on master (1 November 2026, 13:17)

`tools/build_recomp.py` on `master` at `0243e6d` (everything of today's P3 work up to the threaded composite; the later commits are host-side sound, subtitles and docs): exit 0 in 1,658 seconds. 828 of 828 unit tests; coverage added no translated code; all 63 routes identical between the interpreter and the recompiled engine; every translated instruction under both timing profiles; 1,642 matched routines at three seeds (0, 0xC0FFEE and the commit's), 6.88 million states each, 0 mismatching; every routes-only routine ran on a route; none ran past its event limit. Log and work: `D:/f117-gate/gate-master3-gate-20261101/`. Not part of it: the presentation groups of a batch acceptance (N=1 on all routes, boot N=4/9), which the P2 session runs; the app's host code (it is not in the gate).
