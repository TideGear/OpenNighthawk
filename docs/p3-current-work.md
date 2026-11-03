# Stopping checklist: coastline and settings (3 November 2026)

The owner requested a handoff, commit, push and stop. This is a verified partial
stopping point, not completion of Phase 3. Resume the remaining owner requests:

- [ ] Finish a stable world-space Remastered shoreline, anchored to an
  authoritative gameplay water boundary if one exists. The original/GOG issue
  is owner-confirmed; use all twelve session-154150 screenshots and motion.
- [ ] Correct ground alignment across the central view and both widescreen
  margins, including banked views and low-altitude shoreline crossings.
- [ ] Saving and restarting settings must preserve normal SETUP auto-answering
  and apply edited values despite launcher command-line overrides.
- [x] Show nondefault option values and nonempty resource folders in blue.
- [ ] Provide a working reset of all settings to defaults, including runtime
  bindings and command-line precedence; retain resource-folder access.
- [x] Wrap complete fix descriptions inside the Choose fixes menu.
- [ ] Move machine timing, CPU budget, engine and simulation cadence into an
  Advanced Settings submenu.
- [ ] Provide a browsable key-remapping list showing every supported key and
  blue nondefault bindings, with editing and restoration.
- [ ] Group/simplify audio controls while retaining every capability.
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

Unfinished submenus/reset and experimental coastline source are preserved in
`private/settings-20261103/unfinished-snapshot/` (ignored, local only).
`base.json` records original base and source hashes. `resume-wip.patch` is
relative to this stopping point: first run `git apply --check` on it, then apply
only when deliberately resuming. It restores experiments, not accepted fixes.
Do not use `original-wip.patch` over this newer menu commit.
