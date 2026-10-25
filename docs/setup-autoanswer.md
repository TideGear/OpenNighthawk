# Answering SETUP's questions from the settings (design)

Status: design, not implemented. Part of the in-game settings menu, a Phase 3 re-release feature
in [roadmap.md](roadmap.md).

## Purpose

SETUP asks "Do you have a joystick (Y/N)?" and "Select Sound Driver:" before the game proper.
When the settings already say whether a controller is attached and which sound device to use, the
app should type those answers itself instead of making the player type Y/N and 1-4. An option
still shows the prompts for players who want to answer themselves.

## Established

- **Scheduled keys exist headless.** `f117run`'s `--type WHEN:KEYS` types keys at an exact clock,
  an absolute count or `PROG+N` (N clocks after the named program starts; `src/host/headless.c`).
  It is a scheduled, recorded input, so a replay reproduces it exactly. The windowed app
  (`src/host/main.c`) has no equivalent yet.
- **The sound-driver answer is a DOS read, not a polling loop.** SETUP reads it through MISC.EXE's
  `mov ah,1; int 21h; retf` (DOS read with echo) at SETUP `0x00422`-`0x00427`
  ([bugs.md](bugs.md), D3). This machine answers INT 21h AH=01 natively
  (`src/machine/dos_files.c`), so the answer can be supplied at the moment SETUP asks for it, with
  no guessed delay. A main-row digit selects the driver (main-row 1 ends SETUP with exit code 36);
  any extended key quits to DOS, which is how a keypad digit with NumLock off causes D3.

## Open questions

- **Where the joystick question reads its key**, and whether it uses the same MISC.EXE routine.
  This needs a careful reading of SETUP (about an hour of focused work). Locating it is ordinary
  matching work, not a stays-original claim.
- **How the answer is injected.** Either the host recognises the specific read (SETUP running,
  the call from the prompt's site) and supplies the key, or it traps the call site. Either way the
  key must be recorded as an input so replays stay exact.

## Rules

- No fixed `PROG+N` delay as a shipped default: the instruction count at which SETUP reaches a
  prompt need not be stable, and a guessed delay would fail exactly where it matters, on slower or
  faster emulated clocks.
- The answers come from a controller setting (a planned `--controller yes|no`, or detection at
  startup) and the sound-device
  setting (`adlib|roland|pcspeaker|none`, and `hybrid` once
  [hybrid sound](sound-board.md) exists).

## Order of work

1. Locate the joystick prompt's key read in SETUP.
2. Add the injection at both reads, in the headless runner first, recorded in input logs.
3. Wire it to the settings in the windowed app, with the option to show the prompts.

Do this as its own pass, after the Phase 2 integration work.
