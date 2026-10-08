# 86Box's 386DX/33: where emulated time goes

The reference for the 386DX/33 timing profile (roadmap Phase 1). Read from the
86Box source (`D:/86box-src/src/src`, the tree `86box-trace.patch` applies to)
for the profile `D:\86box\vmt386` (`make_profile386.py`): machine `ami495`
(OPTi 495SX), i386DX at 33,333,333 Hz, no FPU, 1 MB, IBM VGA, AdLib,
XT-IDE AT disk (`hdc_1 = xtide_at`, `hdd_01_speed = ramdisk`), dynarec off.
File and line numbers are that tree's (8 Oct 2026). 1 us = 33.333 cycles.

## The CPU

- Core: the `exec386_2386` interpreter (`cpu/386.c`; chosen at cpu.c:1855-1868
  for a 386DX without the dynarec). Each instruction adds its cycles to `tsc`
  and due timers run after every instruction (386.c:406-418).
- Costs are constants in each `ops_2386_*` handler; the shared ones are set at
  cpu.c:1047-1081: ALU r,r 2; r,m 6; m,r 7; m,m 6 (32-bit the same); Jcc taken
  3+4, not taken 3; INT n 37, IRET 22, CALL far 17, RETF 18, JMP far 12.
  Examples: MOV r,m8 4, MOV m,r 2 (x86_ops_mov.h:559, 661); LOOP 11, JCXZ 5
  (+4 taken), JMP/CALL near 7, RET 10 (x86_ops_jump.h); MUL 13/21, IMUL
  14/22/38, DIV 14/22/38, IDIV 19/27/43 (x86_ops_misc.h); IMUL r,rm 14/17;
  MOV sreg,r 2, MOV sreg,m 5, LDS/LES 7; INT3 59, exceptions 70; HLT 100 per
  pass; IN 12, OUT imm 10, OUT DX 11 (x86_ops_io.h).
- A hardware interrupt's entry in real mode costs nothing (386.c:388-401).
- Prefetch (386_common.c:515-567): a 16-byte queue filled 4 bytes at a time.
  An instruction whose bytes drive the queue negative refills it at
  `cpu_prefetch_cycles` per 4 bytes; data reads and writes cost nothing
  directly and only use up cycles that would otherwise refill the queue
  (`mem_cycles`). A taken branch flushes the queue.
- REP string instructions (x86_ops_rep_2386.h): the prefix costs 2 each time
  it is dispatched; per element MOVS 4, STOS 5, LODS 5, CMPS 9, SCAS 8, INS 15,
  OUTS 14. MOVS to SCAS run until about 100 cycles are spent
  (`cycles_end = cycles - 100`, line 217) and are dispatched again, paying the
  2 again; INS and OUTS do one element per dispatch.

## Memory

- `cpu_update_waitstates` (cpu.c:4477-4519): with the external cache flag set,
  2 cycles per 4-byte prefetch and 2 per read or write; without it the CPU
  table's 6. The flag ends up set by the BIOS: OPTi register 0x21 bit 4
  (opti495.c:159-161; ports 0x22/0x24), from the CMOS setup in `vmt386\nvr`.
  Read it from the running VM.
- Code fetched from unshadowed ROM costs 33 cycles per 4 bytes
  (`cpu_rom_prefetch_cycles`, cpu.c:582; mem.c:654-658): the system BIOS at
  F0000, the VGA BIOS at C0000 and the XT-IDE ROM at C8000, unless shadowed
  (OPTi registers 0x22/0x23).

## The VGA

- ISA timing 8/16/32 (vid_vga.c:32) times `isa_cycles` = 4 (cpu.c:577): a byte
  read or write in A0000-BFFFF costs 32 cycles, a word 64, a dword 128, added
  to the instruction (vid_svga.c:1836, 2024, 2253-2440). REP STOSW to A000 is
  5 + 64 a word. The VGA's ports cost nothing extra. (`B86_VGA_FAST`, a local
  test switch, makes them 1/1/2.)
- Mode 13h: a line is 1,059.26 cycles (847.4 displayed), 449 lines a frame,
  475,610 cycles (70.086 Hz). Vertical retrace is set at `vsyncstart`
  (vid_svga.c:1557) and cleared at the CRTC 0x11 line match (1420).

## I/O and timers

- Unclaimed ports and port 0x84 cost `io_delay` = 11 (io.c:384, 442). The PIT,
  PIC, port 0x61, keyboard controller, VGA and IDE ports cost nothing extra;
  CMOS 0x70/0x71 32 each.
- AdLib (Nuked OPL2): a read of 0x388/0x389 costs `isa_timing * 8` with
  `isa_timing = cpuclock / cpu_isa_speed` (snd_opl2_nuked.c:1383, pit.c:1254):
  47 cycles at the chipset's default bus/6 ISA clock (opti495.c:283), 32, 24 or
  20 if the BIOS sets OPTi register 0x25 to /4, /3 or 2/5. Writes cost nothing.
  OPL timers count 80 us and 320 us of emulated time.
- PIT: 8254, 27.9365 cycles a PIT clock, stepped in `tsc`; IRQ0 taken at the
  next instruction boundary.
- DRAM refresh and DMA take no CPU time.

## The disk (XT-IDE AT, a RAM-disk speed preset)

- Every seek or read: 50 us (hdd.c:33). PIO mode 3: 512 bytes in 46.08 us.
- READ SECTORS: 96.08 us to the first DRQ and between sectors (hdc_ide.c:
  1876-1884, 2076-2079), plus the host's transfer loop (REP INSW: 17 cycles a
  word, 4,352 a sector, slower when run from unshadowed ROM).
- READ MULTIPLE 2,000 us before the first data, WRITE 2,000 us before the
  first DRQ then 96.08 us a sector; SEEK and recalibrate 1,000 us.

## Measured: `probe386.py`

`tools/ref86box/probe386.py OUT` runs blocks of one instruction class each
(interrupts off, mode 13h) and reads 86Box's cycle counter at a debug-port
write before each (`B86_PORTLOG`, build_86box.md); this machine logs its
clock the same way (`F117R_PORTLOG`). A block's count includes its mark
(`mov al,n` / `out 0E9h,al`), its setup and the three-instruction restore of
DS and ES (the empty block: 29 cycles, 5 instructions). 86Box counts a REP
instruction once per dispatch, so its instruction count there is the chunks,
not the elements. Measured 8 Oct 2026 on `vmt386`:

| block | instructions timed | 86Box cycles | 86Box instructions counted |
|---|---|---|---|
| empty (the mark itself) | 0 | 29 | 5 |
| nop x64 | 64 | 221 | 69 |
| add r16,r16 x64 | 64 | 157 | 69 |
| mov r16,imm16 x64 | 64 | 157 | 69 |
| mov r16,[si] x64 | 64 | 287 | 70 |
| mov [si],r16 x64 | 64 | 217 | 70 |
| add [si],r16 x64 | 64 | 537 | 70 |
| cmp r16,[bx+si] x64 | 64 | 417 | 71 |
| mov r16,[si+disp16] x64 | 64 | 287 | 70 |
| push/pop x32 | 64 | 221 | 69 |
| lodsb x64 | 64 | 351 | 70 |
| shl r16,1 x64 | 64 | 221 | 69 |
| shl r16,cl(5) x32 | 32 | 127 | 38 |
| jmp short +0 x64 | 64 | 605 | 69 |
| jnz not taken x64 | 64 | 223 | 70 |
| jz taken +0 x64 | 64 | 607 | 70 |
| loop x64 | 64 | 861 | 70 |
| mul r16 x32 | 32 | 703 | 38 |
| div r16 x32 | 32 | 739 | 40 |
| les r16,[si] x32 | 32 | 255 | 38 |
| mov ds,r16 x32 | 32 | 95 | 38 |
| rep movsb 200 | 200 | 853 | 16 |
| rep stosb 200 | 200 | 1053 | 17 |
| rep movsw 200 | 200 | 853 | 16 |
| in al,40h x32 | 32 | 413 | 37 |
| in al,61h x32 | 32 | 413 | 37 |
| in al,dx(388h) x32 | 32 | 1439 | 38 |
| in al,dx(3DAh) x32 | 32 | 415 | 38 |
| mov es:[di],al (A000) x32 | 32 | 1293 | 40 |
| mov al,es:[di] (A000) x32 | 32 | 1315 | 40 |
| rep stosb A000 200 | 200 | 7571 | 76 |
| rep stosw A000 200 | 200 | 14037 | 109 |
| rep movsb A000->A000 200 | 200 | 14383 | 110 |

The figures agree with the source above: 2 cycles a register ALU op, a taken
jump 7 plus the queue refill, MUL 21, DIV 22, IN 12, an AdLib read 12 + 32
(so the BIOS runs the ISA bus at /4, not the chipset's default /6, which
would make it 47), 32 extra a VGA byte and 64 a word.
