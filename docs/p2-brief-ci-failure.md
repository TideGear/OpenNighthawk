# Brief for the P2 session: GitHub runs have failed since 5cd50d8 (1 Nov 2026, 22:00)

The last green run is #643 (`fed0186`). Runs #645 and #648 failed, in the jobs `windows` ("CPU and machine tests") and `munt`
("ROM-free Munt and audio checks"); the others in between were cancelled by the next push (the workflow cancels the older run), which
also sends an email. The first commit that goes red is the one after `5cd50d8` ("feat: stage P2 batch ca pending acceptance").

Reproduced locally on a clean worktree of `28a5923` (everything up to and including `5cd50d8`): `ctest -R code_overrides` fails with
`1 failures`. The list it prints ends with

    VGAME.EXE 0000:0FBD: D34 (destroyed-object lookup) after matched (scene override lookup), a fix in its place

so the new matched routine "scene override lookup" at VGAME `0FBD` has fix D34's patch site (`0xFBD`) inside it, and the test wants
every such pair either declined by the routine (`fix_inside`, `src/matched/matched.c`) or listed as deliberate. The same tree minus
`5cd50d8` passes the whole gate (832 of 832 unit tests; 1,646 matched routines, 0 mismatching; run `gate-master11`). The P3 session did
not touch `matched.c` or the override tables, so this is the staged batch's, and left for the P2 session to decide (decline while D34 is
placed, or place the fix at the routine's entry).

The `munt` job's failure is probably the same test (it builds with the same tree); it was not separately reproduced.
