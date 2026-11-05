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

## Settings restart checked (3 November 2026)

Saving preserves an active explicit sound selection and settings restart removes
conflicting launch options. A real restarted process is checked in the mandatory
host suite: it reads the saved presentation and sound, receives the ordinary N/3
inputs and progresses beyond SETUP. Paths with spaces and a trailing backslash
are included. Sound Driver Ask explicitly shows the questions; factory reset
returns it to Ask. See [host-acceptance.md](host-acceptance.md). This retains the
existing type-ahead schedule; it does not claim the precise-read injection below.

## Not done

- Answering yes to the joystick (SETUP then calibrates).
- A single `hybrid` choice in the sound-driver row ([sound-board.md](sound-board.md)): the effect exists through Music from, Effects from and `--adlib-effects`, but not as one driver option.
- A separate run-SETUP-now action; Sound Driver Ask already shows the prompts on restart.
- Injecting at the DOS key reads instead of by delay. That needs locating the joystick prompt's read in SETUP (about an
  hour of focused matching work) and a change in `src/machine/dos_files.c`, which is P2 gate territory: do it after the
  Phase 2 integration.
