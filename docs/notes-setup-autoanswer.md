Design note (not yet implemented): answering SETUP's joystick and sound-driver
questions from the app's own settings.

**Goal.** When the settings menu already knows whether a controller is
attached and which sound device to use, the app should type SETUP's answers
itself instead of making the player type Y/N and 1-4 by hand (an option
still shows the prompts for players who want to answer themselves).

**What exists already.** `f117run`'s `--type WHEN:KEYS` schedules a key at an
exact clock, either an absolute count or `PROG+N` (N clocks after a named
program starts; see `src/host/headless.c`). It is a scheduled, recorded
input, not a timed guess, so a replay reproduces it exactly. The windowed
app (`src/host/main.c`) has no equivalent yet.

**What is NOT known yet, and needs a real disassembly pass (not light
work):** the exact SETUP.EXE instructions that print "Do you have a
joystick (Y/N)?" and "Select Sound Driver:" and then wait for a key. `PROG+N`
needs a fixed N, which only holds if SETUP reaches the prompt at the same
instruction count every run; if the wait is a polling loop (likely, given
the hardware-wait-loop rule already found elsewhere in this codebase) a
fixed N is fragile. The robust design traps the exact keyboard-wait call
for each prompt (as the project already does for other hardware waits) and
types the answer the instant that call is reached, not after a guessed
delay. This needs:
1. Locating the two prompt/wait routines in SETUP.EXE (own disassembly pass,
   likely an hour of focused reading, not a "light" task).
2. A trap in the host (or a matched/observed address) that fires exactly
   when SETUP is waiting for that specific keypress.
3. Typing the scheduled answer there, from `--controller yes|no` (or
   detected at startup) and the sound-device setting (`adlib|roland|pcspeaker|none`,
   `hybrid` later).

**Do this as its own session/pass**, after Phase 2 integration work, with
the SETUP disassembly done carefully (two-reader rule if it becomes a
stays-original claim; this is ordinary matching work, not a design-rule
exclusion, so no such claim is expected). Do not guess a PROG+N delay as a
shipped default; it would be fragile exactly where it matters (slower or
faster emulated clocks).
