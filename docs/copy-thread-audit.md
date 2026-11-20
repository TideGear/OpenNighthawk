# The render copy on its own thread: shared-state audit

Part B, step 3 (19 Nov 2026). `F117_COPY_THREAD=1` (off by default) runs a Stage 2 scene copy (`rerun_step_run`, the same body the sliced
copy uses) on a thread of its own, start to finish; the display loop only polls (`rerun_step_scene`). `F117_COPY_THREAD=2` makes the loop wait
for the end, which gives a deterministic comparison with the sliced copy (same frame hashes expected).

What the copy's guest code can touch, and the verdict for each:

| State | Where | Verdict |
|---|---|---|
| Observer pointer and its pending records (`g_f117_observer`, `g_pend`, `g_line`, `g_any`, `g_any_display`, `g_pose`) and the observer's scratch buffers | `src/matched/observe.c` | `F117_TLS` (the one change allowed in `src/matched`). Every caller already sets and uses it on the machine's own thread. |
| Fixes' per-run state (`d1_next`, `d34_ext`, `d34_n` and the logged flags) | `src/fixes/fixes.c` | `F117_TLS`. `rerun_step` restores the copy's saved values on the thread that steps it, so the copy thread starts from the snapshot's state. `g_on[]` (switches) is read-only in a flight. |
| `matched.c` | one function pointer (`matched_runner`), the rest is `goto` labels | stateless, nothing to do |
| Runtime tables (`g_rt`: instances, `by_para`, `ok_gen`, `bad_gen`, `entry_of`) | `src/recomp/recomp.c` | read-mostly in a flight. Program loads, unloads and override switches free and rebuild them: `recomp_set_quiesce` makes the loader ask the copy thread to park at a yield point first (`rerun_quiesce_request`), and the job in flight is marked stale and dropped. `gen` bumps are atomic. Counters (`dispatches`, `g_ov_hits`) are diagnostic only; a lost increment changes nothing. |
| Machine statics in `src/machine` | `g_inv_*` (inventory mode, off), `g_find` (DOS file search, not reachable: the copy has no files) | nothing to do |
| `rerun.c` job state (`g_copy`, `g_out`, `g_done`, the far-scenery tables) | `src/present/rerun.c` | owned by the job while it runs. Setters called from the loop each frame (`rerun_set_wide`, `rerun_set_distance`) are plain ints read at begin or at the walk; a change mid-job is a settings change and picks up at the next job. |
| Collector instance (`g_coll`) | `src/host/main.c` | `F117_TLS`; the copy thread sets it once to the Stage 2 scene instance (`copy_thread_init`). |
| Stage 2 helper pool (`g_s2_run`: native batch and commit on the copy thread) vs the blend (display loop) | `remaster_live.c`, `main.c` | the blend gets its own pool (`remaster_live_set_blend_parallel`, `g_bpar`) so two callers never share one. |
| `remaster_scene_dropped`, `remaster_trace_hook` | `remaster_scene.h` | already `F117_TLS`. |

Known limits: the 5 s park timeout prints a warning and goes on (never seen); a job started before a program change is dropped, not
resumed; the thread needs Windows (elsewhere the sliced copy stays).
