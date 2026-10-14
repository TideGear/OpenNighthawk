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
#include "interp.h"
#include "hires.h"
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

/* A replayed frame kept for interpolation: its records, the replay as it
 * stood at its start, the picture it left, and how it pairs with the frame
 * before it. */
typedef struct {
    int32_t       *vals;
    size_t         nvals, cvals;
    interp_rec    *rec;
    int            nrec, crec;
    interp_frame  *parsed;
    drawlist       start;
    hires         *start_hires;   /* the sub-pixel builder at its start (with a scale) */
    uint8_t        picture[64000];
    uint8_t       *picture_hi;    /* and the fine picture it left */
    uint64_t       seq, end;
    int            ok;            /* exact, and no page dump inside */
    interp_pairing *pair;         /* this step paired with the one before it, or NULL */
    int            state;         /* DRAWLIVE_FREE, _STAGED (collected, not yet shown) or _PUBLISHED */
} drawlive_step;

#define DRAWLIVE_POOL 5           /* steps: two published, up to three collected and waiting */
#define DRAWLIVE_QUEUE 16
enum { DRAWLIVE_FREE, DRAWLIVE_STAGED, DRAWLIVE_PUBLISHED };

/* What drawlive_plan_interp decides to show. */
enum { PLAN_NONE, PLAN_REPLAY, PLAN_OLDER, PLAN_NEWER, PLAN_BETWEEN };
typedef struct {
    int      kind;
    double   t;                   /* PLAN_BETWEEN: where between the two steps */
    int      extrapolate;
    uint64_t older, newer;        /* the steps the picture comes from (a log's columns) */
    double   t_shown;
} drawlive_plan;

/* The replay of the feed, a frame at a time: the picture it leaves on the
 * display, and how it compared with the display at each frame's close. With
 * interp set, the last two frames are kept and paired (interp.h), and the
 * picture at a moment between two closes is the in-between frame.
 *
 * Two threads may share it: the owner calls drawlive_collect, which only
 * touches the replay and the free steps, while another thread draws from the
 * published steps with drawlive_render. drawlive_publish changes what is
 * published and must not overlap a render. */
typedef struct {
    drawlist list;
    uint64_t next_seq;
    drawlist pending_start;        /* start of the frame currently being collected */
    hires *pending_hires;
    uint64_t pending_seq;
    size_t pending_at;
    int pending_active;
    int      shown;               /* picture holds a replayed frame */
    const uint8_t *picture;       /* the newest published step's picture */
    uint8_t  blank[64000];        /* what picture holds before any step is published */
    uint64_t picture_seq, picture_end;
    uint64_t frames, exact, inexact, unseeded;
    /* interpolation */
    int            interp, check;  /* check: draw each pair a hair from each end and compare */
    drawlive_step  step[DRAWLIVE_POOL];
    int            ia, ib;         /* the published older and newer steps, -1 none */
    int            newest;         /* the last step kept, staged or published, -1 none */
    int            queue[DRAWLIVE_QUEUE]; /* collected steps (their index) and exact-frame failures (-1), in order */
    int            nqueue;
    interp_list    ilist;
    drawlist       scratch;
    uint64_t       pairs, pairs_exact, pairs_inexact, inbetweens;
    uint64_t       istats[IS_COUNT];  /* what the in-between frames shown moved and held (interp.h) */
    int            extrapolate;    /* the picture's age: 0 a step behind (interpolate), 1 none (extrapolate) */
    /* a picture scale times finer (Stage 2, hires.h); 0 none */
    int            scale;
    hires          *hr, *scratch_hr;
    const uint8_t *picture_hi;     /* the newest published step's fine picture */
    uint8_t       *inter_hi;       /* an in-between frame's */
    const uint8_t *shown_hi;       /* what the last render put in place of the scan, or NULL */
    uint64_t       fine_flat, fine_agree, fine_restored;
} drawlive;

void drawlive_init(drawlive *r);

/* Draw a picture n times finer too (n >= 1): the model polygons refilled from
 * their vertices, the rest scaled; presented in place of the 320x200 one. */
void drawlive_set_scale(drawlive *r, int n);
void drawlive_set_line_width(drawlive *r, int width);

/* Replay the records available, including the open frame, and stage every
 * closed frame the feed holds, releasing it. Open work never replaces the
 * last published picture. A frame that differs from the display at its close
 * is not shown, and the replay is seeded again; so is one after a frame the
 * ring dropped. Stops, leaving frames in the ring, while no step is free.
 * Returns the frames taken. */
int drawlive_collect(drawlive *r, drawfeed *f);

/* Make the staged frames the published ones, in order (the last two are kept,
 * paired). Frees the older steps; nothing may be rendering. */
void drawlive_publish(drawlive *r);

/* drawlive_collect then drawlive_publish: the single-threaded form. */
int drawlive_update(drawlive *r, drawfeed *f);

/* The published older and newer steps, or NULL. */
void drawlive_steps(const drawlive *r, const drawlive_step **older, const drawlive_step **newer);

/* The replayed picture stands for the screen: a replayed frame is held, the
 * machine is in VGAME in mode 13h with the display at A000:0, and the last
 * frame closed less than a quarter second ago (a screen drawn without
 * game_draw shows the scanned picture after that). */
int drawlive_current(const drawlive *r, const machine_t *m);

/* Put the replayed picture in a captured frame (present.h) in place of the
 * scanned-out pixels, keeping its palette: 1 when it did. */
int drawlive_present(drawlive *r, const machine_t *m, present_frame *f);

/* With interp: the in-between picture at the machine's clock now, paced
 * against wall time independently of the game's faster mission clock,
 * between the two frames last closed: the older at the newer
 * one's close, the newer one a step later, so the picture is a step behind.
 * With extrapolate the newer is shown at its own close and the primitives move
 * on as they moved from the older (t from 1 to 2), so the picture has no age
 * added and is a prediction between closes. Falls back to drawlive_present
 * when no pair is held. With a scale, shown_hi is the fine picture shown. */
int drawlive_present_interp(drawlive *r, const machine_t *m, present_frame *f);

/* The same in two parts, so a worker thread can do the drawing. The plan is
 * cheap and reads only the published steps and the machine; icount is the
 * clock the picture is for (the machine's now, or a little later when the
 * picture will be shown a frame on). The render fills f->vram and shown_hi
 * and returns 1 when it put a picture there. A plan for another time or from
 * another publication must not be rendered. */
int drawlive_plan_interp(const drawlive *r, const machine_t *m, const present_frame *f, uint64_t icount,
                         drawlive_plan *plan);
int drawlive_render(drawlive *r, const drawlive_plan *plan, present_frame *f);

#endif
