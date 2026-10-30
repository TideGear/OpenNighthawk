# Other versions of the game

The DOS release (473.04) is what this project reproduces and measures against. MicroProse also
shipped the game on the Amiga (1993) and the Macintosh (1994). Both are later ports with their own
code, so neither is a reference for behaviour, but both carry assets and evidence. Their files are
the owner's own copies in `references/`, which is git-ignored; nothing extracted from them is ever
committed.

**Read, not measured.** Everything below is read from code and data files. Nothing is measured:
the project has no 68k emulator. If a Mac oracle is ever needed it would start from Snow (open
source, MIT; emulates the Mac II with its colour cards; needs the owner's Mac II ROM), with MAME as
a second reference.

**Sources.** Most of the format and asset work is the author's earlier private project (the Reimp,
see [provenance.md](provenance.md)): `docs/formats/cvpc.md`, `docs/re/10-sound.md` and
`docs/re/08-amiga.md`, with readers in its `tools/` (`rsrc.py`, `hfsx.py`, `f117/cvpc.py` and
`f117/macsnd.py` for the Mac; `f117/adf.py`, `f117/amigacat.py` and `f117/amigaexe.py` for the
Amiga). They are used from that project's folder, not vendored here. The Mac code readings (timing
and the bug comparison) are this project's, from capstone disassemblies of two builds the owner
holds: **2.1.0** (the program in the `F117a Stealth Fighter 2.1` folder of the 2.3.2 archive, whose
version resource says "Version 2.1.0, 1994") and **2.3.2** (inside `F117A Version 2.32 Update.image`,
code 248,520 bytes against 252,860). Addresses below are 2.3.2 unless marked 2.1.0.

## Macintosh (2.1.0 and 2.3.2, 1994)

- **Files.** The owner's archives in `references/Mac Version`. `unar` (The Unarchiver's
  command-line build, run from a user folder without installing) extracts the 2.3.2 archive to a
  private folder outside the repository (`%USERPROFILE%\f117-recomp-local\mac-extract`).
- **Art.** 77 `CvPc` resources, decoded: a big-endian header, a palette, then GIF LZW. They are
  named after the `.GIF` files they came from, which pairs each with its DOS `.PIC`. The cockpit
  ships at 640x480 and 512x384; palette index 64 (cyan) is the chroma key the 3-D scene shows
  through. The HUD is a different implementation: long scrolled tapes, not DOS's drawn lines.
- **Sound.** 33 `snd ` resources in `F117a Sound` (1.1 MB, 8-bit mono, 7.4-11.1 kHz, named),
  decoded to WAV in the private folder: 20 effects (304-320, and 323-325 for the bay door, hangar
  door and impact), the engine loop (500), three pieces of music (title, death and good mission;
  resources 200, 321 and 322) and nine speech words (1000-1008) that the game chains into phrases.
  The port kept the DOS effect ids and added 300: DOS ids `0x04`-`0x14` are resources 304-320, checked
  against 13 ids of independently known meaning. The music is DOS ids 36 to 38 (resources 322, 321 and 200, by ear).
  The bay door (323) goes with DOS 31 and 29, the impact (325) with 30; the engine loop (500) is request 311/312. These
  recordings are a column of the [sound board](sound-board.md), which has the evidence, and feed hybrid sound.
- **Data.** The world data is byte-identical to DOS, so the two differ in presentation and code.
- **Code.** 13 `CODE` resources (252,860 bytes) in `F117a Stealth Fighter 2.0`, built with THINK C
  6 and later 7. It carries no MacsBug function names: its only four symbols (`GETWDINFO`,
  `HCREATE`, `HDELETE`, `HOPEN`) are file-system glue from the compiler's interface library, so it
  offers no names to borrow for DOS routines.
- **Timing: DOS's S with the cap raised to 60.** Every frame `CODE13:05824` (2.3.2) takes the
  `TickCount` difference, keeps a history of frame times under 30 ticks, and every 8 frames sets
  S = 60 / (average ticks per frame), clamped to `[4 - time_scale, 60]` (`058A4`-`058D4`, `[b8e6]`);
  game rates divide by it at dozens of sites. DOS caps S at 15. 2.1.0 also ported DOS's frame wait
  (`CODE13:0123C`) but it only runs above S 60, so it never fires; 2.3.2 removed it.
- **A speed setting (2.3.0 onward).** `[b8e8]` selects Realistic (0, S measured), Quick (3), Fast (2)
  or Warp (1); a nonzero setting pins S to that value and skips the measurement
  (`CODE13:0589A`, `01360`). Menu handler `CODE02:040AC`; the 2.3.0 notes warn that Warp makes AI
  aircraft "crash a little too often" and missiles less accurate, which matches the S-dependent
  per-frame steps below. The pinned values 1-3 are below DOS's floor of 4.
- **Bug catalogue against 2.3.2** (full sweep, with the Q-series IDs of
  [bugs.md](bugs.md#smaller-defects-q-series); all read from code, none measured):
  - *Present, same mechanism:* D1 (Realistic setting only), D1TTL (`03FB2`-`03FC0`, signed `4*S`
    guard; launch `0314C`-`031E4`), D1SLOT (`04268` cancel, `03CB8` decrement; call order traced so
    no path skips it), D1PROX (`03F8C`-`03F9E`, `(speed << 3) / S`), D1ACCEL (`0383A`-`03858`), D4
    (mission table `CODE02:00D90`; the world data is byte-identical), D5 (`03D3A`-`03D5C`), D6
    (`032C0`-`03314`), D7 through D8 (`03C16`-`03C26`), D12 (`CODE07:005B2`), Q9, Q10, Q11, Q13a
    (`CODE13:0327C`), Q13b (`CODE08:0092C`), Q14 (the overdue string, `CODE12:0198C`), Q19 (Enemy
    Troops, `CODE08:027CE`), Q21 (was D75, `CODE10:0207A`), Q22 (the armoury, `CODE05:017B8`) and the
    roster fix-up (`CODE05:0531C`).
  - *Changed:* D1REAL (no real-time pacing; the speed setting pins S instead), D11 (the roster is
    overwritten in place, not truncated: `CODE05:054DC`-`055A0`), D34 (worse: records are 10 bytes
    and record 31 overwrites the table's own count, `CODE12:01F54`), Q5 (`CODE09:01EB6` skips a
    negative child[0]), Q8 (a 32-bit `z + r` bound), Q18 (was D17: the same loop at `CODE08:00E14`, with
    zeros after the table, so Dec 31 is followed by "Mar 1"), D18 (no "(null)" literal) and the terrain
    lookup above level 4 (returns `level - 5`).
  - *Fixed:* Q3 (`CODE12:01FE8` clears the register), Q7 (`CODE11:03864` recomputes the range) and
    Q12 (`CODE10:00CAC` passes the unit hit).
  - *Not applicable:* D2, D3, D35, D36 and the sound-driver defects (DOS-only code).
  - *Undetermined:* Q2, Q17 (was D61), Q20 (the popup), D98 and D101 (no counterpart found); D97 is
    absent.
  - *Consequences of the raised cap:* the D1PROX radius at S 60 is a quarter of DOS's at 15; an SA-5's
    16-bit lifetime `2400 * S` goes negative for S 14-27 and wraps from S 28; D1ACCEL's per-second
    acceleration is four times DOS's. These bite in the Realistic setting only.
- **Mac-only defects.** D10 (mountain-collision stack corruption), from MicroProse's 2.3.0 notes
  ([bugs.md](bugs.md#d10-mountain-collision-stack-corruption-macintosh)), is located: 2.1.0
  `CODE09:011CA`-`011D0` pushes a word and pops four bytes (`addq.l #4`); 2.3.2 `011C4`-`011CA` pops
  two. Also noticed, unreported by MicroProse: in Realistic the S computation divides by the average
  frame time with no zero guard (`CODE13:058AC`, 2.1.0 `06672`) and frames of under one tick are
  recorded, so a fast enough machine could divide by zero; and flight set-up at `CODE13:04C4C` looks
  as if it starts a pinned-speed game at S 4 until the speed is changed (suspected, not run).
- **Aside.** Its fonts are named `CivBabylon`, `CivZulu` and so on, reused from MicroProse's
  Macintosh *Civilization*.

## Amiga (3.01, 18 October 1993)

- **Files.** Three 880 KB floppies. Disks 1 and 2 are cracked releases (their boot blocks and
  loader may be patched); disk 3 is untouched data.
- **Art and models.** Same nine theatres and object roster as DOS; the models (`.SBN`) are
  big-endian and laid out differently from `.3DX`.
- **Sound.** Five IFF 8SVX samples, embedded in the intro player `play2` and the front end `f117`:
  an explosion, missile fire and three beeps (2.8-17.9 kHz, made with AudioMaster III). The flight
  engine `f117f` holds no 8SVX; a 54 KB chip-memory data hunk in it is probably raw sound data, not decoded (the owner
  judged the Amiga sounds not worth using). The front
  end still names the DOS drivers (`Asound.117` and the others), a remnant of the port.
- **Code.** `f117f` is one flat 140 KB code hunk with absolute relocations, so its
  cross-references resolve statically where DOS's go through load-time-patched jump tables. That
  makes it a map for orienting a reading of DOS code reached through pointers; it is never a
  reference for behaviour, being a later revision on a different CPU and renderer.
- **Timing: DOS's S, capped at 10.** The Amiga did not replace S. Each frame
  `f117f:0x4106D4`-`0x4106EC` sets S = 64 / (ticks this frame), clamped to 3-10, in `[43E1D8]`
  (halved under time compression, `0x4106F2`-`0x4106FE`); the mission second advances every S
  frames (`0x410712`-`0x410720`); and 31 sites divide a rate by S (DOS: 32), including player
  position, airspeed and heading. The tick is a CIA-B timer at 64.00 Hz (PAL), so 64 / ticks is
  frames per second, and a 6-tick floor (`0x40005C`) matches the cap of 10. The elapsed-time scale
  `[43B6A2]` (tick count x 2048, `0x400022`-`0x400038`) is real but feeds only 17 sites:
  chase-camera and view smoothers, audio slew limits and animation ramps - none of the flight model,
  AI or weapons. The earlier project's note that the Amiga replaced S with elapsed time is wrong
  (read from the loop). Like the Mac, the Amiga kept frame counting and picked
  a cap to suit its machines.
- **Bug catalogue against v3.01** (with the Q-series IDs of
  [bugs.md](bugs.md#smaller-defects-q-series); read from code, none measured; addresses are
  `f117f` unless marked `f117:` for the front end):
  - *Present:* D1ACCEL (`0x40E638`-`0x40E656`, +1 on odd frames), D1PROX (`0x40EEBC`-`0x40EECC`,
    `(speed << 3) / S`, S 3-10: reach 75 units at S 3, 22 at S 10, DOS 15 at S 15), D4 (mission
    table at `f117:0x42A32E`, entries 0-3 identical; the worlds are compressed `.WLD` files that
    decompress to DOS's layout with the same zero class bytes at LB, ME and NC), D6 (`0x40E1E4`-
    `0x40E210`; the decompressed worlds have the same 20 cover-0 cells), D8 (`0x40EAE8`-`0x40EAFC`),
    D11 (`f117:0x4307D4`, `"wb"` in place), D12 (`0x455328`-`0x455352`), D34 with a bound of 128
    records (`0x41B8F6`; the overflow runs into the mission records and then the cover grid), the
    roster fix-up (`f117:0x42FFE2`), Q19 (Enemy Troops, `f117:0x43BEA6`), Q21 (was D75, `0x412548`),
    Q22 (the armoury, `f117:0x4347D6`), and D7 through D8.
  - *Fixed:* D1TTL (lifetime = `range << 4` x S / terminal speed, at most 24,000, so the signed
    guard cannot see a wrapped value), D1SLOT (the decrement at `0x40EBCC`-`0x40EBD4` tests for
    zero first), D5's gate (admits type `0x26`; the credit inside `0x40F5BA` is not traced), Q3
    (`0x41B9CC` clears the register), Q5 (word children, negative meaning none, `0x4090C0`), Q13a
    (range and bearing zeroed, `0x40E2EA`, so a sensorless unit always announces and never launches),
    Q13b (`f117:0x43BA5C`), Q14 (`0x416E54`) and Q17 (pitch clamped and the index mirrored,
    `0x402554`, `0x4025FE`).
  - *Changed:* D1 (S capped at 10, no averaging, no frame wait, plus an unguarded `divu` at
    `0x4106DC`), D1REAL (bounded, not real time), Q18 (was D17: the same unbounded loop, but it reads
    0 then `b` past the table and shows "Feb N" of the next year), Q2 (no offset tables), Q4 (no
    calibrated divide), Q8 (a full frustum test, `0x4087E4`), Q9 (the player's zoom for every kind),
    Q10 (a look-at camera) and D18 (no "(null)" literal).
  - *Not applicable:* D2, D3, D35, D36 and the sound-driver defects. *Undetermined:* D10, Q7, Q11,
    Q12, Q20, D98, D101 and the terrain lookup; D97 is absent.
  - *Amiga-only, unreproduced:* the proximity test uses `bgt` where DOS uses `JGE`
    (`0x40EEE2`-`0x40EEE8`), so a slot-8 player weapon would take the incoming-missile hit branch.
  - Unscaled per-frame rates exist (missile launch delay, ground-site alert gain, takeoff pitch),
    as they do on DOS; nothing measured shows the Amiga is more frame-dependent. The reports of a
    "broken flight model" are not explained by any scaling gap found here.
