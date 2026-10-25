# Other versions of the game

The DOS release (473.04) is what this project reproduces and measures against. MicroProse also
shipped the game on the Macintosh (1994) and the Amiga (1993). Neither can serve as a reference
here - there is no 68k oracle in the project, and both are later ports with their own code - but
both carry assets and evidence. Their files are the owner's own copies in `references/`, which is
git-ignored; nothing extracted from them is ever committed.

Most of what is known comes from the author's earlier private project (F-117A Reimp), whose
`docs/formats/cvpc.md`, `docs/re/10-sound.md` and `docs/re/08-amiga.md` hold the detail, with its
readers in `tools/` (`rsrc.py`, `hfsx.py`, `f117/cvpc.py`, `f117/macsnd.py`, `f117/adf.py`,
`f117/amigacat.py`, `f117/amigaexe.py`).

## Macintosh (2.0 to 2.3.2, 1994)

- **Art.** 77 `CvPc` resources, decoded: a big-endian header, a palette, then GIF LZW. They are
  named after the `.GIF` files they came from, which pairs each with its DOS `.PIC`. The cockpit
  ships at 640x480 and 512x384; palette index 64 (cyan) is the chroma key the 3-D scene shows
  through. The HUD is a different implementation: long scrolled tapes, not DOS's drawn lines.
- **Sound.** 33 `snd ` resources, decoded: 8-bit mono at 7.4-11.1 kHz, named - 17 effects, an
  engine loop, three pieces of music and nine speech words the game chains into phrases. The Mac
  port kept the DOS sound ids and added 300: DOS effect ids `0x04`-`0x14` are Mac resources 304-320,
  checked against 13 ids of independently known meaning. These recordings are the fourth column of
  the planned sound board ([notes-sound-board.md](notes-sound-board.md)).
- **Data.** The world data is byte-identical to DOS, so the two differ in presentation and code.
- **Code.** 13 `CODE` resources (252,860 bytes), built with THINK C 6 and later 7. It carries no
  MacsBug function names (only four library-glue symbols), so it offers no names to borrow.
- **Bugs.** D10 (mountain-collision stack corruption) is a Mac defect, from MicroProse's 2.3.0
  release notes ([bugs.md](bugs.md)).
- **Timing: DOS's S, with the cap raised to 60.** `CODE13:01238` is a port of DOS `timing_recompute`
  (`0x0D441`): the same frame-wait formula `clamp((9 - 120/S) >> 1, 1, 4)` and the same clamp
  `[4 - time_scale, cap]` on S, with the cap 60 (the Mac's tick rate) instead of 15. Per frame,
  `CODE13:06584` takes `TickCount` minus the previous reading, keeps a history of frame times under
  30 ticks, and every `4*S` frames sets S = 60 / (average ticks per frame) / time_scale (`[b8e6]`);
  game rates divide by it (`divs.w [b8e6]` at dozens of sites). `CODE13:00566` halves S when time
  compression goes to 2. The time-scale cell (`[b900]`) and the clamp helper (`jsr $922`) are
  read from their arguments and the match with DOS, not traced further. So the 1994 Mac port kept
  frame counting (the 1993 Amiga did not), and treated 15 as a platform ceiling it could raise.
  Read 24 Oct 2026 from a capstone disassembly of the owner's 2.3.2 copy.
- **Asides.** Its fonts are named `CivBabylon`, `CivZulu` and so on, reused from MicroProse's
  Macintosh *Civilization*.

## Amiga (v3.01, 18 October 1993)

Three 880 KB floppies; disks 1 and 2 are cracked releases (their boot blocks and loader may be
patched), disk 3 is untouched data. Same nine theatres and object roster as DOS; the models
(`.SBN`) are big-endian and laid out differently from `.3DX`.

- **Code as a map.** The flight engine `f117f` is one flat 140 KB code hunk with absolute
  relocations, so its cross-references resolve statically where DOS's go through load-time-patched
  jump tables. Useful to orient a reading of DOS code reached through pointers; never a reference
  for behaviour, since it is a later revision on a different CPU and renderer.
- **Timing.** By this version MicroProse had replaced the DOS frame-counting rate `S` with an
  elapsed-time scale (1.15 fixed point, saturating at 16 ticks) and a floor of 6 ticks per frame,
  applied through one multiply idiom at all 18 rate sites. That supports reading D1 as an
  assumption about machine speed rather than gameplay intent. It is not evidence for the 15/3
  oscillation once reported for DOS, which this project did not reproduce ([bugs.md](bugs.md), D1).
- **Sound.** Five IFF 8SVX samples are embedded in the intro player and the front end (an
  explosion, missile fire, three beeps; 2.8-17.9 kHz, made with AudioMaster III). The flight engine
  holds no 8SVX; where its in-flight sounds come from is not established. The front end still names
  the DOS drivers (`Asound.117` and the others), a remnant of the port.
