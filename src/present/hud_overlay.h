/* hud_overlay.h - the state-driven HUD in the replay (docs/presentation.md, "The state-driven HUD").
 *
 * With a scale N > 1, the observer's 'h' record (the HUD pass's starting state, hud_state.h) makes the
 * replay draw the tapes, the heading strip and the pitch ladder on the fine grid from the state itself,
 * and the records of the pass that those replace (hud_cover.h) keep their coarse effect but leave the fine
 * picture alone. The panel pass's flight-path marker and its two bars are drawn the same way from the
 * observer's 'p' record (their records are recognised on arrival). N = 1 and a log without 'h' and 'p'
 * records are the original's replay unchanged. */
#ifndef F117R_HUD_OVERLAY_H
#define F117R_HUD_OVERLAY_H

#include "drawlist.h"

/* Called first by drawlist_record: 1 when it handled the record (an 'h', or a record of the pass the fine
 * picture replaces, which it ran with the fine mirror off), 0 to let the replay handle it as usual. */
int hud_overlay_hook(drawlist *d, char kind, const int32_t *v, int n);

/* The origin tags the state-driven HUD gives the diagnostic plane (diagtag.h's own enum is left alone). */
enum { DT_HUD_STATE_LINE = 240, DT_HUD_STATE_TEXT = 241 };
#define HUD_STATS 11

/* A check for tests: with it on, every record the fine picture replaces is run between two copies of the fine
 * page, and a record that changed it is counted ("leaked"). Slow; off by default. */
void hud_overlay_debug(int on);

/* On by default; F117_PRESENT_HUD=old turns it off (the earlier HUD, for comparisons). */
void hud_overlay_enable(int on);
int hud_overlay_enabled(void);

/* What the HUD did on this replay: HUD passes seen, passes drawn from state, passes left on the original
 * path (no driver tick table, no font yet, no page), records that left the prediction, records replaced and
 * kept, unsupported states. */
void hud_overlay_stats(const drawlist *d, uint64_t out[HUD_STATS]);
const char *hud_overlay_stat_name(int k);

#endif
