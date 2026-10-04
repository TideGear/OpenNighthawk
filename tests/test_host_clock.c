#include "host_clock.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(void)
{
    uint64_t discarded = 0;
    const uint64_t ips = 9000000;
    CHECK(host_clock_target(1000000000, ips, ips, &discarded) == ips);
    CHECK(discarded == 0);
    /* 800 ms stall allows only 100 ms of catch-up. The next frame must
     * advance normally, without repeatedly discarding an expanding debt. */
    uint64_t target = host_clock_target(1800000000, ips, ips, &discarded);
    CHECK(target == 9900000 && discarded == 6300000);
    CHECK(host_clock_target(1810000000, ips, target, &discarded) == 9990000);
    CHECK(discarded == 6300000);
    CHECK(host_clock_target(2800000000, ips, 18900000, &discarded) == 18900000);
    CHECK(discarded == 6300000);
    CHECK(host_clock_target(0, ips, 0, &discarded) == 0);
    return 0;
}
