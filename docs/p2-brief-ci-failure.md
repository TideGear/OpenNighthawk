# Why the Windows runs failed after 5cd50d8

The failures came from an unaccepted Phase 2 candidate, not the Phase 3 renderer. Its 34 VGAME far-jump thunk registrations overlap the dynamically installed graphics observers at 29 addresses. The matched thunk takes dispatch priority there, so the observer cannot capture the graphics entry. Whitelisting those overlaps would hide a real loss of Remastered input.

On 2 November, the pending VGAME thunk family was removed from the mainline registry, fixture, and CTest registration. This restores the existing observer path. All 34 private proofs remain available, but none of these thunks is integrated or earns credit; five entries without observer collisions can be reconsidered later as a separate, scoped batch. The four sound tails and seven SETUP wrappers remain unaccepted pending candidates.

The workflow also canceled earlier runs when later pushes arrived. The P3 handoff is a frequently updated working note and is not read by tests, so it is now ignored by the Windows workflow. Code changes and the other CI inputs still trigger the checks.
