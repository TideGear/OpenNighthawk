# Phase 2 completion plan — 1 November 2026

Proposal for the fastest completion under the existing accuracy and scope
rules. This is a plan, not acceptance evidence. No score or implementation
changes are made by this document.

## Diagnosis and priorities

The accepted scoreboard is P2 96.93%. `tools/progress.py` reports:

| Remaining item | Current gap | P2 percentage points remaining, approximately |
|---|---:|---:|
| All-code naming and matching | 17,361 / 209,298 score bytes | 2.613 |
| Route-executed matching | 1,261 / 163,907 distinct bytes | 0.415 |
| Reviewed references outside routes | 218 / 47,952 distinct bytes | 0.045 |

These measures overlap; do not add their byte totals. The census can count
overlapping row bytes, whereas route coverage counts distinct bytes. Closure
requirements also remain beyond these numbers. About 85% of the remaining
weighted score is in the all-code item. Finishing only the current routes
cannot finish Phase 2.

The recent sessions produced useful private proofs, but accepted throughput
stalled at integration. Pending batch `ca` has 457 body bytes: 250 route bytes
and 207 static bytes, forecast to earn about 0.11 P2 points. Its private audit
passed 169 instructions and 286 mutations. The full harness was never freshly
built or accepted. `tests/func_lockstep.c` is about 4 MB, with fixture logic
inside `main`; the handoff records memory exhaustion despite compiler-option
trials. Repeating those trials is a poor next use of time.

Other avoidable costs are repeated full-source builds for private proofs,
tiny batches, fixture repair after expensive runs, and changing shared inputs
while proofs are in flight. Batch `bz` acceptance alone cost 2,503 seconds
(about 42 minutes). Its 244 accepted route bytes moved P2 by 0.12 points.

## 1. Unblock compilation and accept the existing work

Use one isolated P2 worktree and one pinned integration base. Preserve all
owner/P3 edits. Recheck the base and current processes before starting: the
shared branch has advanced beyond the handoff's P2 base.

1. Inventory `ca`'s exact body/fixture hashes, private verdicts, seed tallies,
   mutations and manual integration rows. Reuse unchanged successful proofs;
   refresh evidence where its actual inputs changed. Do not treat a directory
   name or an old executable as proof of freshness.
2. Mechanically extract the huge fixture section into bounded functions,
   initially in the same C file with inlining disabled for those functions.
   Preserve every predicate, execution order, random draw, reset, local value
   and side effect. Do not turn independent fixture conditions into an
   exclusive dispatch. Split into translation units only if bounded functions
   still exceed a measured memory/time budget. An include-file split alone
   does not reduce the compiler's function complexity.
3. Validate the extraction against the last buildable accepted harness at
   identical source/seed inputs: selected fixture-state digests, RNG state,
   comparison/skip counts and named instruction coverage. At the current
   candidate revision, rerun all new CTests, whole-table seeds and the
   required harness regressions. Keep known negative controls failing.
   If extraction changes any proof inputs, regenerate the affected proofs.
4. Build the complete current harness with production settings. Record command,
   source and executable hashes, duration and peak compiler memory. A stale
   harness linked to a new library or a pruned private runner cannot accept
   the combined batch.
5. Run `ca`'s audit, route prechecks, required whole-table seeds and one full
   grouped acceptance, including optimized seeds, both timing profiles,
   presentation checks and final credit reconciliation. Refresh the score
   only after the verdict passes against the exact tree.

Treat the structural compile fix as the first deliverable; do not expand into
a compiler/toolchain migration unless the bounded-function approach fails
with concrete measurements. Reserve the roughly 42–56 minutes observed for
acceptance separately from build and repair time; this is not a completion
promise.

## 2. Produce one exact remaining-work ledger

Create the ledger from accepted census and coverage inputs, using
`tools/census_classify.py` and its existing row listing, then add the static,
encoded and reference inventories. Reconcile to all three totals above.
Use canonical module/linear addresses and resolve segment aliases before
assigning work. Each range needs:

- Exact extent, image hash, existing named overlap and net eligible credit.
- Route/static/encoded classification and current review status.
- Body, fixture and mutation dependencies; existing proof/evidence location.
- Next concrete action, owner, blocker and last measured stage duration.
- Independent review and closure status, distinct from checklist credit.

Keep accepted and pending inventories separate. A read-only classifier run
against current source plus `bz` coverage correctly fails because 45 pending
registrations lack accepted coverage (34 thunks, seven SETUP entries, four
sound entries). Generate the baseline ledger at the accepted revision, or
after `ca` passes with its fresh coverage. Never make the old packet appear
current by inserting unaccepted rows.

Resolve stale records once: the 10,301-byte static report versus the current
reviewed-static inventory; 218 missing reference bytes; the historical
probable-dead remainder; Q6 entry/dead certificates; and the protected note's
five stale units. Reference completeness alone does not close matching work.

## 3. Run a pipeline focused on the large static gap

Schedule from the ledger by expected new eligible bytes per measured proof
hour, dependencies and closure risk. Prefer coherent routine families with
reusable *validated* fixture patterns. Similar code in another module still
needs proof against its own bytes and behavior.

Initial queues, with exact sizes/order decided by the reconciled ledger:

| Queue | Work | Completion evidence |
|---|---|---|
| Static library families | SETUP libraries, formatting arms, picture/stream helpers, arithmetic and graphics helpers | Full private proofs, static route-neutrality replay, integration and gate |
| Route remainder | Remaining sound, PLAYER, VGAME and small program tails | Full proofs plus real route invocation and parity |
| Entry and classification closure | Dispatch tables, callback/vector writers, overlays, self-modification, static discoveries | Second-read evidence for every entry path or a valid exact-range certificate |

Static code already has owner authorization for lockstep-only acceptance plus
static neutrality replay. Do not spend hours inventing routes for those
ranges. Machine-option-only errors use fixture inputs, without adding test
machine options. Preserve the explicit PLAYER `00D5F–00DAF` exception: it
still requires a route selecting grayscale. Investigate that trigger early.
Also resolve ASOUND.117 `09F2`'s D2-fix interaction and ISOUND.LOG `0581`'s
exact invalid-range boundary early; neither should hold unrelated proofs.

When execution is authorized, use at most four agents including the
coordinator: two independent proof workers and one blind review/closure
worker. The coordinator integrates, verifies evidence and schedules builds.
The existing workflow explicitly calls for independent readers. Work may
continue on review while compilation is serialized; agent concurrency does
not authorize memory-heavy build concurrency. Measure available RAM and
per-build peaks rather than relying on the old three-compiler limit. Stop all
proof workers for gates.

Start closure review with the first proof wave. In particular, resolve
computed entries, writable callbacks and interrupt registrations, the 585
encoded SETUP code bytes, and the packed stub's separate representation and
design-rule review. Validated discovery adds workload where required. Do not
count the packed and unpacked addresses as the same code. Default to matching
unresolved code; pursue exclusion only when a strict certificate is feasible.

## 4. Reduce repeated work without reducing proof

- Build private candidate-sized bodies against hash-pinned unchanged
  libraries. Reuse the existing private-kit approach. If the modular harness
  supports per-module private runners, retain all relevant shared fixtures
  and reset logic and verify their input states against the full harness.
  Pruned runners remain development tools; acceptance uses the complete one.
- Review original bytes, boundaries, body, fixture and mutations together
  before building. Reuse corrected Ghidra exports; decompilation is a lead.
- Run the existing 64-state smoke at all required seeds first. Then retain
  every full coverage, seed, mutation, checkpoint and optimized check. Require
  positive comparisons and inspect skips even when the exit code is zero.
- After 20 minutes of stalled diagnosis or two failed repairs, retain the
  failing state, first divergence and next discriminating experiment; switch
  to another ledger item. Return with a specific hypothesis. No endless
  rebuilds or repeated successful runs without changed inputs or a concern.
- Pin interpreter/cache inputs for each wave. Coordinate P3 changes through
  explicit integration points. Reuse cache only under the verified key and
  preserve the required fresh-baseline audit.
- Integrate approximately 30–50 independently ready routines per batch,
  preferring 1,000–1,500 new eligible bytes. Static bytes count toward naming
  throughput; they must not be presented as route credit. Gate `ca` once it is
  buildable rather than making completed work wait for an arbitrary quota.
- Before every gate, verify the registry/coverage delta, all manual harness
  rows and private audits. Keep accepted packets immutable; record permitted
  historical coverage refreshes as separate hashed addenda.

## 5. Measure delivery and close on evidence

After each batch publish accepted new route bytes, accepted all-code score
bytes, unresolved ranges, review findings and time in compilation, proofs,
integration, gate and repair. Ready-but-unaccepted bytes are a separate line.
Every session should deliver an accepted batch, a verified infrastructure
improvement, or a concrete resolved blocker with reproducible evidence.

Set a completion forecast only after the first representative static wave:
remaining ledger workload divided by measured proof throughput, plus measured
build/gate overhead and the remaining closure tasks. Do not extrapolate the
speed of five-byte thunks to formatter/interrupt/encoded-code work. The
current evidence supports a multi-batch effort, not an honest hours-to-100%
promise.

The final batch must pass on the published source with all required tests,
full parity, timing, event limits, optimized proofs and presentation checks.
Then independently reconcile zero unmatched route bytes, zero uncredited
all-code work except valid exclusions, zero missing reference bytes, and zero
unresolved entry/classification findings. Reconcile docs and accepted hashes.
A rounded 100.00%, a reviewed-but-held row or an absence of captured execution
does not establish completion. Use the existing completion requirements in
`docs/progress.md` without changing weights, scope or accuracy thresholds.
