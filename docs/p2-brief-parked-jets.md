# Brief for the machine-layer session: the parked jets that GOG's DOSBox has and this machine does not

Found 1 November 2026 while chasing the owner's report R7 (`docs/owner-issues.md`). This is a fidelity difference between the emulated PC and GOG's DOSBox, not a renderer problem.

**Symptom.** On the Siconella training takeoff (the owner's `session-20261031-230426`), GOG's DOSBox shows two small black aircraft shapes on the horizon just left of the control tower (the structure at the far end of the runway) and the HUD targets a MIG-25 there. This machine's own VGA picture never has them (`f117a --presentation original`, and `f117run`), and the flight program never asks for a model in their place.

**Reproduce.**
- DOSBox side: `py tools/dosbox_flight.py --data DIR --out OUT --replay-input LOG9 --set-clock-from-log --shot-seconds 84,87,90 [--dump-seconds 80,84]`. `LOG9` is the session's `input.log` with `ips=9000000` in the header, the K and M clocks multiplied by 9/60, J lines dropped and mouse deltas zeroed (the DOSBox replay loader accepts nothing else). `DIR` is a copy of the GOG install with the session's `save-start` files laid over it. Screenshots land in `OUT/vgame_NNN.png`, RAM dumps in `OUT/ram_N.bin` (1 MB, the guest's memory).
- This machine: `f117a --replay input.log --presentation original` with `F117_DUMP_RAM=CLOCK:FILE` for a memory dump and `F117_PRESENT_SHOTS=PREFIX F117_PRESENT_SHOTS_FROM=CLOCK` for pictures.
- Find the flight program's data segment in a dump with `find_psp(ram, b"VGAME")` from `tools/dosbox_flight.py`: DS = (psp + 0x10 + 0x1E42) << 4.

**What was ruled out** (each tested): the fix set (`--fixes original`), the machine speed (replay at 9 M instructions a second), the DOS clock and the mission's random seed (eight delayed launches give eight seeds and none has the jets), the engine (`--engine interp` gives the same seed and no jets), the CPU's divide-error return rule, the DOS memory-block layout, and the data (the mission files START writes, `lb.3dx`, `photo.3dx`, `stflt.3dx`, are byte-identical and the loaded blocks are identical in memory).

**Where the two memories differ** (DS of VGAME, same point of the takeoff roll):
- `DS:49F4`, the number of objects placed in the frame's draw list: 11 in DOSBox, 9 here. The list itself (22-byte records from `DS:4A20`, pointers at `DS:4E70`) has two extra entries at its head in DOSBox: records `DS:4AE6` and `DS:4AFC`. Here those two records are zero. The routine that adds a record is at VGAME image offset `0DB3C` (`imul ax, [49F4], 0x16; add ax, 0x4A20`), called from `compose_defer_model` (`D9FE`) and `compose_emit_object` (`E2B6`): so the two jets are world objects that the scene scan places in DOSBox and does not place here.
- The targets table at `DS:B2CE` (16-byte records) has the same places and types but different counters: the airfield record's words 3 and 4 read 2 and 2 in DOSBox, 1 and 0 here (across nine runs here the second is almost always 0), and the object slot numbers in word 6 are 1 to 9 in DOSBox and 0x3D or 0x7E and up here.

**Suggested next step.** Find what writes `DS:B2CE+16*i+6` and `+8` (the airfield's counters) and what decides that the scene scan places objects 8 and 9: a write watch on those words in this machine, or an instruction trace from `vg_init_airborne`, then ask which input to that code (a DOS or BIOS answer, a port read, a timer value, a file's length) differs from DOSBox. DOSBox's own debugger build is not needed: the guest memory dumps above are enough to compare state.
