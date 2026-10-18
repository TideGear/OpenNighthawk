#ifndef F117R_FRAME_PACER_H
#define F117R_FRAME_PACER_H
#include <stdint.h>

/* Simulation frames per machine second. Keep fractional clocks so clocks
 * not divisible by the rate still advance exactly one second per hz frames.
 * Overruns catch up without discarding simulated time. Blocking
 * dialogs explicitly clear the phase instead of guessing from elapsed time. */
typedef struct {
    uint64_t next, ips, epoch;
    unsigned remainder, hz;
} frame_pacer;

/* Preserve the remaining fraction of an update, including overdue work,
 * when compression changes. Restarting or keeping the old deadline makes
 * rapid compression changes gain or lose mission time. */
static inline void frame_pacer_rate(frame_pacer *p, uint64_t now,
                                    uint64_t ips, uint64_t epoch, unsigned hz)
{
    if (p->ips != ips || p->epoch != epoch) {
        p->next = 0; p->remainder = 0; p->ips = ips; p->epoch = epoch;
    }
    if (p->hz && p->hz != hz && p->next) {
        if (p->next >= now) p->next = now + (p->next - now) * p->hz / hz;
        else p->next = now - (now - p->next) * p->hz / hz;
        p->remainder = 0;
    }
    p->hz = hz;
}

static inline uint64_t frame_pacer_wait_rate(frame_pacer *p, uint64_t now,
                                            uint64_t ips, uint64_t epoch, unsigned hz)
{
    frame_pacer_rate(p, now, ips, epoch, hz);
    if (p->next && now < p->next) return p->next;
    if (!p->next) {
        p->next = now; p->remainder = 0;
    }
    p->next += ips / hz;
    p->remainder += (unsigned)(ips % hz);
    if (p->remainder >= hz) { p->next++; p->remainder -= hz; }
    return 0;
}

static inline uint64_t frame_pacer_wait(frame_pacer *p, uint64_t now,
                                       uint64_t ips, uint64_t epoch)
{
    return frame_pacer_wait_rate(p, now, ips, epoch, 8);
}
#endif
