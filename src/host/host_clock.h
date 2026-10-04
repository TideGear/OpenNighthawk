/* Keep host stalls from becoming permanent emulated-clock acceleration. */
#ifndef F117R_HOST_CLOCK_H
#define F117R_HOST_CLOCK_H
#include <stdint.h>

static inline uint64_t host_clock_target(uint64_t elapsed_ns, uint64_t ips,
                                         uint64_t current, uint64_t *discarded)
{
    uint64_t wall = (uint64_t)((double)elapsed_ns * (double)ips / 1e9);
    uint64_t target = wall > *discarded ? wall - *discarded : 0;
    uint64_t limit = current + ips / 10;
    if (target > limit) {
        *discarded += target - limit;
        target = limit;
    }
    return target;
}
#endif
