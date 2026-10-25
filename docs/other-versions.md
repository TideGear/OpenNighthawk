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
code 248,520 bytes against 252,860). An earlier version of this page read 2.1.0 and called it 2.3.2;
addresses below are 2.3.2 unless marked 2.1.0.

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
  against 13 ids of independently known meaning. The music's DOS ids are not yet confirmed. These
  recordings are a column of the planned [sound board](sound-board.md).
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
- **Bug catalogue against 2.3.2** (full sweep, 24 Oct 2026; all read from code, none measured):
  - *Present, same mechanism:* D1 (Realistic setting only), D1TTL (`03FB2`-`03FC0`, signed `4*S`
    guard; launch `0314C`-`031E4`), D1SLOT (`04268` cancel, `03CB8` decrement; call order traced so
    no path skips it), D1PROX (`03F8C`-`03F9E`, `(speed << 3) / S`), D1ACCEL (`0383A`-`03858`), D4
    (mission table `CODE02:00D90`; the world data is byte-identical), D5 (`03D3A`-`03D5C`), D6
    (`032C0`-`03314`), D7 through D8 (`03C16`-`03C26`), D12 (`CODE07:005B2`), D17's mechanism, D75,
    the Q9, Q10, Q11, Q13a and Q13b defects, and the roster, armoury, overdue-string and Enemy
    Troops-overlay defects.
  - *Changed:* D1REAL (no real-time pacing; the speed setting pins S instead), D11 (the roster is
    overwritten in place, not truncated: `CODE05:054DC`-`055A0`), D34 (worse: records are 10 bytes
    and record 31 overwrites the table's own count, `CODE12:01F54`), Q8, D18, and the terrain lookup
    above level 4.
  - *Fixed or absent:* Q3 and Q12.
  - *Not applicable:* D2, D3, D35, D36 and the sound-driver defects (DOS-only code).
  - *Undetermined:* Q2, Q5, Q7, D61, D97, D98, D101, D103 and the briefing popup (no counterpart
    found).
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
  engine `f117f` holds no 8SVX; where its in-flight sounds come from is not established. The front
  end still names the DOS drivers (`Asound.117` and the others), a remnant of the port.
- **Code.** `f117f` is one flat 140 KB code hunk with absolute relocations, so its
  cross-references resolve statically where DOS's go through load-time-patched jump tables. That
  makes it a map for orienting a reading of DOS code reached through pointers; it is never a
  reference for behaviour, being a later revision on a different CPU and renderer.
- **Timing: S replaced.** MicroProse replaced the DOS frame-counting rate S with an elapsed-time
  scale (1.15 fixed point, saturating at 16 ticks) and a floor of 6 ticks per frame, applied
  through one multiply idiom at all 18 rate sites. That supports reading D1 as an assumption about
  machine speed rather than gameplay intent. It is not evidence for the 15/3 oscillation once
  reported for DOS, which this project did not reproduce ([bugs.md](bugs.md), D1).
- **Bugs.** Not yet compared with the DOS entries.
