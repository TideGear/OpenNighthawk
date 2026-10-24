# Reviewed static code: reachability audit

The 21 October analysis covered 10,301 reviewed static bytes that the 63-route
interpreter captures leave unexecuted and uncredited (the current uncredited
total, which includes later reviews and the encoded SETUP rows below, is in
[roadmap.md](roadmap.md)). It identified no additional route, which does not
prove that no input can reach them. All these bytes remain in the Phase 2
denominator; this review certifies no new dead code.

The resumed independent review checked 27 ranges (6,505 bytes) in groups A and
D-H against the current installed images: hashes, complete instructions, exact
ends and zero observed execution agree. Its direct walk does not resolve every
indirect transfer, callback registration, interrupt vector or pointer writer.
Absence of a direct reference cannot close those entry paths.

| Group | Static bytes | What is established; what remains |
|---|---:|---|
| A: VGAME raster helpers | 1,042 | Normal startup selects M. The reviewed `1377:0204..0356` range also contains EGA and MCGA pixel routines; it is not exclusively CGA/Tandy. Caller and pointer-writer closure remains open. |
| B: formatting arms | 1,251 | Scanned format literals use no modifier states. Variable or constructed format arguments were not fully resolved; some reviewed rows also contain executed arms. |
| C: DSWAP formatting core | 1,104 | Identified callers require floppy-read success or DOS allocation/free failure. The owner declined floppy support; machine-option-only error paths remain original code, not dead code. |
| D: SETUP library cluster | 2,724 | No reachable direct root identified. Indirect runtime calls and callback-table writers remain unresolved. |
| E: picture/stream/span wrappers | 1,480 | No reachable direct caller identified. Writable and stack-supplied callback domains need closure. |
| F: signal and INT 23h shims | 569 | The candidate registration functions install real handlers. Their registration, handler fallthrough and dynamic callback paths must be checked together. |
| G: MPS_LOGO mouse helpers | 230 | Initialization installs an INT 33h callback. Registration and callback pointer writers need closure. |
| H: PLAYER report formatter | 460 | The report helper's direct root has no identified caller. Computed entries and runtime callbacks remain unresolved. |
| I: VGAME spawn/3D helpers | 1,441 | No reachable direct root identified. Computed entries remain unresolved; one reviewed row includes an executed tail. |

## Corrections to the earlier route plan

VGAME startup passes literal M to `1377:000C`. Its dispatch table at `007C`
includes pixel entry `0287`, inside the reviewed range `0204..0356`. Wrapper
`0043` selects that slot through the table. The wrapper itself is not executed
by the captures, so this establishes neither a live player route nor deadness.
It disproves the earlier argument that selecting M excludes the whole range.
The initial table pointer is `0070` (EGA), and initialization changes it to
`007C`; proving all writers and entries requires more than inspecting that
normal initialization.

The saved MPS_LOGO pointer scan used header SS `041A` as its data segment.
Relocated startup and callback code use `0332`. The corrected scan's lack of
candidate initializers remains limited negative evidence. Mouse initialization
at `00DA:04DA` installs callback `00DA:0677` through INT 33h; that callback can
dispatch through a writable far pointer. Signal registration similarly installs
INT 23h handlers in PLAYER and MPS_LOGO, with fallthrough into reviewed code.
These mechanisms must appear in any later per-range certificate.

Whole reviewed rows must not be removed merely because some of their arms
appear unused. Formatting rows in PLAYER and MPS_LOGO, and VGAME's
`120A:0EE9` row, include executed bytes. Any eventual certificate must describe
precise byte ranges and preserve all accepted matching credit.

## Evidence and adoption rule

Private evidence is in `D:/f117-gate/static-route-verify-20261021/`:
`resumed-candidates.tsv/json`, `resumed-audit.py/txt`, `resumed-report.md` and
the superseding `group-a-closure.md` with its table/writer inventory. The
earlier `static-route-plan-20261021/` remains a route-planning lead, with its
categorical unreachability assertions superseded by this review.

A later exclusion needs a certificate for each exact range, covering direct,
computed, callback, vector, fallthrough and relevant writer paths under the
supported machine and input conditions. Until that evidence exists, retain
the workload. Lack of current route coverage is not a certificate.

## Encoded SETUP code adopted (24 October 2026)

`census-encoded-code.json` pins two independently reviewed code ranges at
SETUP `01E1:0050..00CE` and `01E1:0141..030C`: 585 bytes / 268 instructions.
`tools/encoded_code.py` validates them during every census refresh. It pins
the shipped file and immutable image, bootstrap and complete source block,
transform-region hash, complete decoded-block hash and both row hashes.
Only the fixed XOR with the low byte of segment-relative IP over
`01E1:0050..0564` is supported. Code rows must decode to whole instructions
with exact counts and overlap neither another encoded row nor a plain review.
Relocations in the source block are rejected. The installed file and module
image remain untouched; decoded bytes exist only in memory.

The bootstrap transforms 1,300 bytes. Of these, 585 are code and 715 are
metadata, scratch and prompt data; transforming bytes does not make them code.
The accepted rows add 585 workload bytes, zero matched credit. Second-read
`unreached-held` reference entries account for those bytes in the checklist.
No actual entry route or dead certificate is established. The media-check helpers,
BIOS disk retries, saved DOS vector and reencoding tail remain proof work.

The first semantic review and independent transform audit agree on all byte
hashes, lengths, code/data boundaries and unresolved entry paths. Private
evidence: `static-review-20261021/followup-review-report.md` and
`static-route-verify-20261021/setup-xor-independent-report.md`. Five synthetic
validator tests cover altered hashes, phase/bounds errors, overlaps,
relocations, partial instructions, wrong counts and absent review evidence.
