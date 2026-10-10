#include "frame_pacer.h"
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void)
{
    for (uint64_t ips = 9000000; ips <= 9000007; ips++) {
        frame_pacer p = {0};
        uint64_t now = 1000;
        for (unsigned frame = 0; frame < 8000; frame++) {
            CHECK(frame_pacer_wait(&p, now, ips, 1) == 0);
            const uint64_t next = 1000 + (uint64_t)(frame + 1) * ips / 8;
            CHECK(p.next == next);
            /* Multiple event/IRQ boundaries while waiting cannot consume a frame. */
            CHECK(frame_pacer_wait(&p, now + 1, ips, 1) == next);
            CHECK(frame_pacer_wait(&p, next - 1, ips, 1) == next);
            now = next;
        }
    }
    frame_pacer p = {0};
    CHECK(frame_pacer_wait(&p, 100, 800, 1) == 0);
    CHECK(p.next == 200);
    CHECK(frame_pacer_wait(&p, 250, 800, 1) == 0); /* short overrun catches up */
    CHECK(p.next == 300);
    CHECK(frame_pacer_wait(&p, 1000, 800, 1) == 0); /* heavy work cannot lose time */
    CHECK(p.next == 400);
    CHECK(frame_pacer_wait(&p, 1020, 800, 2) == 0); /* new program */
    CHECK(p.next == 1120);
    CHECK(frame_pacer_wait(&p, 1030, 1600, 2) == 0); /* changed machine speed */
    CHECK(p.next == 1230);
    p.next = 0; p.remainder = 0;                   /* pause */
    CHECK(frame_pacer_wait(&p, 9999, 1600, 2) == 0);
    CHECK(p.next == 10199);
    printf("frame pacer: exact cadence, event waits, lossless catch-up and resets; %d failures\n", failures);
    return failures != 0;
}
