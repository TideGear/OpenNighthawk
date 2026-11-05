# Current checklist: coastline, settings, and scenery distance (3 November 2026)

This checklist is dated 3 November. For the renderer's speed work and the owner's 4 November goal, the section at the top of `handoff.md` and the last section of
[gpu-compositor.md](gpu-compositor.md) are newer and take precedence.

Stopped at the owner's request for a handoff to a new conversation. Phase 3
remains incomplete. Read `handoff.md` before resuming. The latest draw-distance
work is recorded below and in [scenery-distance.md](scenery-distance.md).

Work resumed at the scheduled 8 a.m. Pacific time on 3 November. Phase 3 is
incomplete. The shoreline correction is now installed for owner review; the requested settings batch is now implemented and integration-tested:

- [x] Correct the reported Remastered shoreline motion using a common camera
  and complete-scene background ownership. All twelve views and a moving
  regression pass; owner confirmation/broader terrain coverage remain pending.
  No authoritative gameplay water boundary was established or changed.
- [x] Correct the same scene's ground projection/composition across the centre
  and widescreen margins, including ground near clipping at each ring scale.
  Banked wide-ground native tests pass; broader owner viewpoint review is pending.
- [x] Saving and restarting settings must preserve normal SETUP auto-answering
  and apply edited values despite launcher command-line overrides.
- [x] Show nondefault option values and nonempty resource folders in blue.
- [x] Provide a working reset of all settings to defaults, including runtime
  bindings and command-line precedence; retain resource-folder access.
- [x] Wrap complete fix descriptions inside the Choose fixes menu.
- [x] Move machine timing, CPU budget, engine and simulation cadence into an
  Advanced Settings submenu.
- [x] Provide a browsable key-remapping list showing every supported key and
  blue nondefault bindings, with editing and restoration.
- [x] Group/simplify audio controls while retaining every capability.
- [x] Describe each existing setting, resource folder and root menu action.
  New controls added later must also have descriptions.

Original-game parity is a hard boundary. A Remastered shoreline correction
belongs in the presentation override or throwaway render copy, never in the
live simulation, collision rules, original terrain data or Original renderer.
Settings conveniences must use normal guest inputs for SETUP answers. Keep
applicable machine-parity checks and host acceptance separate, both required;
do not launch another full machine gate without the owner's authorization.

Validate each feature through its real app path as well as focused tests.
Review rendered menus for clipping, defaults coloring and usable navigation.
Keep Original/Enhanced controls, compare simulation state for presentation
isolation, and retain existing depth/light/margin regressions. Scored commit
prefixes come from `tools/progress.py --title`; do not change scores to claim
unverified work. Preserve unrelated owner files.

The prior ignored settings/coast experiments are superseded by accepted source.
Do not apply their old patches over the current worktree. Current settings
evidence: `private/settings-20261103/settings-release-verified/`, including
eight actual menu/restart subcases. See [settings.md](settings.md).

The full Phase 3 goal remains active: the roadmap/progress rows still require
renderer throughput, longer scenery reach, broader motion/visibility/HUD and
terrain coverage, remaining controls/accessibility/replay/audio capabilities,
and the applicable owner/reference checks. This checklist covers the latest
owner reports; it does not replace that scope or authorize another full gate.

## Remastered scenery distance (3 November 2026)

- [x] Enable a three-cell additional terrain walk in the Remastered wide-view
  render copy. A fresh North Cape airborne land/coast frame shows 177 changed
  pixels at the widened edge; setting the environment override to 0 restores
  the control. The normal game simulation and Original/Enhanced live hook limit
  remain unchanged.
- [x] Preserve the full eight-word camera call and install both private copy
  hooks even when all eight live hooks are occupied. The focused rerun test
  passes, including the full-slot condition and live-memory isolation.
- [ ] Check distant buildings and terrain on several land-heavy flights,
  moving/turning views, ring transitions, duplicated geometry and coast
  interaction. Measure performance beyond the current single-scene counts.

The three-cell result receives partial credit only. The prior taxi samples and
the first land comparison with no installed scenery hook are not distance
acceptance evidence. The later post-turn sky capture is an exact no-change
negative sample. Details and current-build hashes are in
[scenery-distance.md](scenery-distance.md). No full machine gate or ONTests
refresh was done.

Also audit held-key release across host overlays and remapping before claiming
complete controls coverage: current menu scripts open shortcuts through modifier
flags, without preceding physical modifier key-down events. This is a coverage
gap to investigate, not a verified new gameplay defect.
