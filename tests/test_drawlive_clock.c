/* ROM-free playback-clock regression: the first replayed frame need not
 * have a usable predecessor. Its absence must not switch clock domains. */
#include "drawfeed.h"
#include "machine.h"
#include <stdio.h>
#include <string.h>

static drawlive r;
static machine_t m;
static present_frame f;
static char pair_token; /* planner checks presence only; it never reads the pairing */
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static double position(const drawlive_plan *p)
{
    if (p->kind == PLAN_NEWER || p->kind == PLAN_REPLAY) return (double)p->newer;
    if (p->kind == PLAN_OLDER) return (double)p->older;
    return (double)p->older + p->t;
}

static void startup(int snapshots)
{
    memset(&r, 0, sizeof r);
    memset(&m, 0, sizeof m);
    m.ips = 60000000; m.video_mode = 0x13;
    m.nproc = 1; strcpy(m.procs[0].name, "VGAME.EXE");
    r.rerun = r.smooth = r.shown = 1;
    r.ia = -1; r.im = 0; r.ib = 1;
    r.ema = 4000000; r.pull_in = .05;
    r.step[0].seq = 1; r.step[0].end = 4000000;
    r.step[1].seq = 2; r.step[1].end = 8000000;
    r.picture_seq = 2; r.picture_end = 8000000;
    drawlive_plan p;
    m.cpu.icount = 12000000; /* publication can lag a close while the worker draws */
    CHECK(drawlive_plan_interp(&r, &m, &f, m.cpu.icount, &p));
    /* Frame 1 was unseeded: pair 1/2 is unavailable. Frame 3 now closes,
     * while the smoothed position still lies before the first valid pair. */
    r.ia = 0; r.im = 1; r.ib = 2;
    r.step[2].seq = 3; r.step[2].end = 12000000;
    r.step[2].pair = (interp_pairing *)&pair_token;
    r.step[1].have_compose = r.step[2].have_compose = snapshots;
    r.picture_seq = 3; r.picture_end = 12000000;
    double previous = 0;
    for (uint64_t clock = 14700000; clock <= 21000000; clock += 300000) {
        m.cpu.icount = clock;
        CHECK(drawlive_plan_interp(&r, &m, &f, clock, &p));
        const double at = position(&p);
        CHECK(at >= previous);
        CHECK(at >= 2 && at <= 3);
        CHECK(p.kind == PLAN_BETWEEN && p.sa == 1 && p.sb == 2);
        previous = at;
    }
}

int main(void)
{
    startup(1);
    startup(0);  /* copy unavailable: draw-list fallback uses the same clock */
    if (failures) return 1;
    puts("playback clock: startup and copy fallback remain monotonic");
    return 0;
}
