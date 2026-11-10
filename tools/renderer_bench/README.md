# Renderer benchmark and guard scripts

Scripts for the Remastered renderer's speed work (plan and results: [../../docs/gpu-compositor.md](../../docs/gpu-compositor.md), last section; the goal and next steps: the top of [../../handoff.md](../../handoff.md)).
They are Git Bash scripts for the owner's Windows machine and need the game install (`GAME`, default `D:/GOG/F-117A`, never written) and the recorded runway flight.

- `bench.sh NAME DISTANCE [NAME=value ...]`: replays the runway flight (clocks 3.9 to 4.8 billion) and prints the mean, p50, p95 and p99 of the work per frame (`cost_ns` of `--present-log`).
  `MODE=gpu` runs a hidden Direct3D 11 window; `WIN=3840x2160 PSCALE=9` is 4K. Extra `NAME=value` arguments become environment variables (`F117_GPU_COMPOSE=1 F117_GPU_PRESENT=1 F117_NO_VSYNC=1`
  for the GPU path; with vsync on the GPU renderer blocks in its draw calls, so benchmark with `F117_NO_VSYNC=1`).
- `guards.sh checksums TAG`: composed-image checksums at fixed cadence at Near, Far and Maximum, compared with the stored baseline; `guards.sh baseline` rewrites the baseline from the current build;
  `guards.sh statehash` prints the replay's final machine hash for the ONTests build and the current one.

A speed change is accepted only when every distance prints `IDENTICAL` and the final machine hashes agree.

**Data, kept out of the repository** in `private/renderer-bench/` (git-ignored): `session-20261103-154444/` (the recorded flight: `input.log` and `save-start`), `f117a.ini` (the ONTests ini, whose fix list the
replay needs: without it the replay crashes near clock 3.54 billion, owner issue R28) and `baseline/hash_base_{near,far,maximum}.txt` (taken from the build at `03c6631`; 901 checksums each). If they are lost,
regenerate the baseline with `guards.sh baseline` from a build at or before `85ec934`; the flight is the owner's session `20261103-154444` under `%APPDATA%\OpenNighthawk\sessions`.

Worker-stage diagnostics: set `F117_WORKER_TIMING` to an output CSV path. The presentation worker writes one row per job, with `frame`, `got`, `composed`, `gpu_begin_ns`, `picture_ns`, `compose_ns`, `total_ns`, `gpu_wait_ns` (how long the compose call blocked on the GPU fence) and `gpu_span_ns` (from the scene's submission to that fence; it overlaps the picture layer). The stages measure CPU elapsed time around GPU begin/submission, `drawlive_render`, and `remaster_apply_ex`. Their sum is `total_ns`; the semaphore wait and CSV write are excluded. `got` and `composed` identify jobs that produced and composed a picture. The file is buffered and closes when the worker exits.

These are elapsed times on the worker thread, including waits inside each call. They are not CPU execution time or GPU timestamp measurements: GPU work can overlap picture drawing, and composition can wait for it. Keep diagnostic runs separate from acceptance timing runs, since CSV output adds work. Leave the variable unset for normal play and performance acceptance. Compare picture checksums and the final machine hash with diagnostics enabled before accepting an instrumentation change.