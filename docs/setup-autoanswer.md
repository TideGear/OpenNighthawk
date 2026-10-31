# Answering SETUP's questions from the settings

Part of the in-game settings in [roadmap.md](roadmap.md) (Phase 3). The sound-driver answer is implemented; the joystick
answer is a fixed "no".

## Purpose

SETUP asks "Do you have a joystick (Y/N)?" and "Select Sound Driver:" before the game proper. When the settings already
say which sound device to use, the app types the answers itself instead of making the player type N and 1 to 3. The
default, `ask`, still leaves the questions to the player.

## Implemented (30 October 2026): `--sound`

`--sound adlib|roland|speaker|ask` (`sound =` in `f117a.ini`, "Sound driver" in the settings menu) types SETUP's two
answers: `N` (no joystick) 200,000 instructions after SETUP.EXE starts, then the driver's digit (1 speaker, 2 AdLib, 3
Roland) after 2,000,000, each held 540,000 instructions. These are the numbers the gate's routes
(`boot_to_flight.args`) use, and they pass under both engines. The app hooks the program-start event the recompiled
runtime already uses, so no machine code changed; the keys are recorded as inputs like any other, and the answers are
skipped for a replay (which brings its own). Checked in the real app: `--sound adlib` loaded `Asound.117`, `roland`
`Rsound.117`, `speaker` `Isound.117`, and `ask` waited at SETUP.

This is a fixed delay, which the rules below warn against. It is safe here because the machine is deterministic (the
same instruction count every run) and the answers are type-ahead, but it assumes the first question is always the
joystick one.

## What was established

- **Scheduled keys exist headless.** `f117run --type WHEN:KEYS` types keys at an exact clock, an absolute count or
  `PROG+N` (N clocks after the named program starts; `src/host/headless.c`), recorded so a replay reproduces it.
- **The sound-driver answer is a DOS read, not a polling loop.** SETUP reads it through MISC.EXE's
  `mov ah,1; int 21h; retf` (DOS read with echo) at SETUP `0x00422` to `0x00427` ([bugs.md](bugs.md), D3). This machine
  answers INT 21h AH=01 natively (`src/machine/dos_files.c`). A main-row digit selects the driver (main-row 1 ends SETUP
  with exit code 36); any extended key quits to DOS, which is how a keypad digit with NumLock off causes D3.

## Rules

- No fixed delay as a shipped default where a precise injection is possible: the instruction count at which SETUP reaches
  a prompt need not be stable on other clocks.
- Every injected key is recorded as an input so replays stay exact.

## Not done

- Answering yes to the joystick (SETUP then calibrates).
- A `hybrid` choice ([sound-board.md](sound-board.md)).
- An option to show the prompts only when asked.
- Injecting at the DOS key reads instead of by delay. That needs locating the joystick prompt's read in SETUP (about an
  hour of focused matching work) and a change in `src/machine/dos_files.c`, which is P2 gate territory: do it after the
  Phase 2 integration.
