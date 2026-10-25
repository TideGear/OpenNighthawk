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
and the incoming-missile defects) are this project's, from a capstone disassembly of the owner's
2.3.2 copy on 24 October 2026.

## Macintosh (2.0 to 2.3.2, 1994)

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
- **Timing: DOS's S, with the cap raised to 60.** `CODE13:01238` is a port of DOS
  `timing_recompute` (`0x0D441`): the same frame-wait formula `clamp((9 - 120/S) >> 1, 1, 4)` and
  the same clamp `[4 - time_scale, cap]` on S, with the cap 60 (the Mac's tick rate) instead of 15.
  Each frame `CODE13:06584` takes `TickCount` minus the previous reading and keeps a history of
  frame times under 30 ticks; every `4*S` frames it sets S = 60 / (average ticks per frame) /
  time_scale (`[b8e6]`). Game rates divide by it (`divs.w [b8e6]` at dozens of sites).
  `CODE13:00566` halves S when time compression goes to 2. The time-scale cell (`[b900]`) and the
  clamp helper (`jsr $922`) are identified from their arguments and the match with DOS, not traced
  further. So the 1994 Mac port kept frame counting (the 1993 Amiga port did not) and treated 15
  as a platform ceiling it could raise.
- **Incoming-missile defects** (all in `CODE13`, on the same 28-byte object records):
  - *D1PROX, present, so a smaller radius at the higher cap.* `04A72`-`04A88` computes the reach
    as `(speed << 3) / S` and admits a hit only when it exceeds a slant-style distance (horizontal
    distance plus `|altitude difference| >> 5`), followed by a slot-below-8 and a `4*S` age guard:
    the DOS shape. At S 60 the hit radius is a quarter of DOS's at its cap of 15; the porters raised
    the ceiling without touching a radius that depends on it.
  - *D1TTL, present over a wider range.* A ground launch (`038DA`-`03932`) starts at speed 1 with
    lifetime `range * 16 * S / speed`, stored as a 16-bit word; the hit guard (`04AA0`-`04AB0`)
    skips damage while the lifetime is at or below `4*S` as a signed word, as DOS does. An SA-5
    (range 150) gets `2400 * S`: a negative word for S 14-27, so it cannot hit until the count falls
    below 32,768 (the DOS defect over a wider S range), and from S 28 the product wraps, leaving
    erratic short lives (S 28: 1,664 frames; S 55: 928).
  - *D1ACCEL, present.* `040B6`-`040D2` (and its twin at `041AE`) adds one speed unit on odd
    frames (`btst #0` of the frame counter) up to the type's terminal speed, with no division by
    S, so acceleration per second scales with S: four times DOS's S 15 rate at the cap of 60.
  - *D1SLOT, plausible, not confirmed.* The movement path decrements the lifetime with no zero
    test (`046E6`), as DOS's `0x6CA6` does, and a cancel path zeroes it (`04DF6`); whether a
    cancelled slot reaches the decrement depends on call order across routines, not traced.
- **Other bugs.** D10 (mountain-collision stack corruption) is a Mac defect, from MicroProse's
  2.3.0 release notes ([bugs.md](bugs.md#d10-mountain-collision-stack-corruption-macintosh)).
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
