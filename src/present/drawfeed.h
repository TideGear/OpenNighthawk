/* drawfeed.h - the live path's first part (docs/presentation.md): the
 * observer's records handed to the host in memory, a logic frame at a time,
 * and the Stage 1 replay (drawlist.h) run on them as they arrive.
 *
 * A logic frame is the phases (game_draw calls) from one step's picture to
 * the next: it closes at the first phase after one that presented the work
 * page (graphics entry 44), as tools/interp_frame.py groups them. Closed
 * frames wait in a ring until the host takes them. The feed is an observer
 * (observe.h) and only reads: it asks the original for nothing but the
 * records and, once, the pages to seed a replay with, and at each frame's
 * close it copies the display as it stands, so the replay can be checked. */
#ifndef F117R_DRAWFEED_H
#define F117R_DRAWFEED_H

#include "observe.h"
#include "drawlist.h"
#include "present.h"

#include <stddef.h>
#include <stdint.h>

#define DRAWFEED_FRAMES 8         /* ring slots: the frame being collected and up to 7 closed */

typedef struct {
    uint64_t seq;                 /* frames closed before this one */
    uint64_t start, end;          /* the clock at its first phase, and at the phase that closed it */
    int      phases;
    int      seeded;              /* it holds page dumps ('Z', 'Y', 'J') */
    int32_t *rec;                 /* each record: kind, n, the clock's low and high words, n values */
    size_t   len, cap;
    uint8_t  display[64000];      /* A000 at the close: what the replay must equal */
} drawfeed_frame;

typedef struct {
    f117_observer  obs;           /* observe_set(&feed->obs) */
    const uint8_t *mem;           /* guest memory, read at each frame's close */
    drawfeed_frame frame[DRAWFEED_FRAMES];
    unsigned       head, count;   /* the oldest closed frame and how many are closed */
    int            presented;     /* the frame being collected has presented */
    int            want_seed;     /* dump the pages at the next phase */
    uint64_t       closed, dropped;
} drawfeed;

/* Set up (the observer included; install it with observe_set(&f->obs)). */
void drawfeed_init(drawfeed *f, const uint8_t *mem);
void drawfeed_free(drawfeed *f);

/* The oldest closed frame, or NULL; drawfeed_pop releases it. */
const drawfeed_frame *drawfeed_oldest(const drawfeed *f);
void drawfeed_pop(drawfeed *f);

/* Have the next phase bring the pages again. */
void drawfeed_reseed(drawfeed *f);

/* Walk a frame's records from *at (start at 0): 0 at the end. */
int drawfeed_next(const drawfeed_frame *fr, size_t *at, char *kind, uint64_t *icount, const int32_t **v, int *n);

/* The replay of the feed, a frame at a time: the picture it leaves on the
 * display, and how it compared with the display at each frame's close. */
typedef struct {
    drawlist list;
    uint64_t next_seq;
    int      shown;               /* picture holds a replayed frame */
    uint8_t  picture[64000];
    uint64_t picture_seq, picture_end;
    uint64_t frames, exact, inexact, unseeded;
} drawlive;

void drawlive_init(drawlive *r);

/* Replay every closed frame the feed holds and release it. A frame that
 * differs from the display at its close is not shown, and the replay is
 * seeded again; so is one after a frame the ring dropped. Returns the
 * frames taken. */
int drawlive_update(drawlive *r, drawfeed *f);

/* The replayed picture stands for the screen: a replayed frame is held, the
 * machine is in VGAME in mode 13h with the display at A000:0, and the last
 * frame closed less than a quarter second ago (a screen drawn without
 * game_draw shows the scanned picture after that). */
int drawlive_current(const drawlive *r, const machine_t *m);

/* Put the replayed picture in a captured frame (present.h) in place of the
 * scanned-out pixels, keeping its palette: 1 when it did. */
int drawlive_present(const drawlive *r, const machine_t *m, present_frame *f);

#endif
