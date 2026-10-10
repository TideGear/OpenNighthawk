#ifndef F117R_FRAME_PACER_H
#define F117R_FRAME_PACER_H
#include <stdint.h>

/* Eight simulation frames per machine second. Keep fractional clocks so
 * clocks not divisible by eight still advance exactly one second per eight
 * frames. Overruns catch up without discarding simulated time. Blocking
 * dialogs explicitly clear the phase instead of guessing from elapsed time. */
typedef struct {
    uint64_t next, ips, epoch;
    unsigned remainder;
} frame_pacer;

static inline uint64_t frame_pacer_wait(frame_pacer *p, uint64_t now,
                                       uint64_t ips, uint64_t epoch)
{
    if (p->ips != ips || p->epoch != epoch) {
        p->next = 0; p->remainder = 0; p->ips = ips; p->epoch = epoch;
    }
    if (p->next && now < p->next) return p->next;
    if (!p->next) {
        p->next = now; p->remainder = 0;
    }
    p->next += ips / 8;
    p->remainder += (unsigned)(ips % 8);
    if (p->remainder >= 8) { p->next++; p->remainder -= 8; }
    return 0;
}
#endif
