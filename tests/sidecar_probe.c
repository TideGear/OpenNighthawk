/* sidecar_probe ASOUND.117 [id] [-v]: stage the AdLib driver as a sidecar, raise an id and run ticks; print the OPL writes.
 * Exit status 1 when the driver does not initialise, a call does not return, id 0 makes sound, or the id makes none (the
 * install is needed, so this is a gate-side tool like check_drivers, not a ctest). */
#include "sidecar.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned writes, keyons;
static int verbose;
static uint64_t write_hash;
static uint8_t registers[256];
static unsigned active_voices(void)
{
    unsigned active = 0;
    for (unsigned reg = 0xB0; reg <= 0xB8; ++reg) active += (registers[reg] & 0x20) != 0;
    if (registers[0xBD] & 0x20) active += (registers[0xBD] & 0x1F) != 0;
    return active;
}
static void opl(void *user, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)user;
    writes++;
    registers[reg] = val;
    write_hash = (write_hash ^ reg) * UINT64_C(1099511628211);
    write_hash = (write_hash ^ val) * UINT64_C(1099511628211);
    if (reg >= 0xB0 && reg <= 0xB8 && (val & 0x20)) keyons++;
    if (verbose) printf("  %llu: %02X <- %02X\n", (unsigned long long)icount, reg, val);
}
int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sidecar_probe ASOUND.117 [id] [-v]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *b = malloc((size_t)n); if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    const unsigned id = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 0) : 6;
    verbose = argc > 3;
    sidecar_t *s = sidecar_new_adlib(b, (size_t)n, opl, NULL);
    if (!s) { printf("init failed\n"); return 1; }
    printf("init: %u writes\n", writes);
    unsigned w0 = writes;
    printf("request %u: %s\n", id, sidecar_request(s, (uint16_t)id) ? "returned" : "DID NOT RETURN");
    printf("  %u writes, %u key-ons\n", writes - w0, keyons);
    w0 = writes;
    for (int t = 0; t < 200; t++) if (!sidecar_tick(s)) { printf("tick %d did not return\n", t); return 1; }
    printf("200 ticks: %u writes, %u key-ons\n", writes - w0, keyons);
    const unsigned made = writes - w0;
    sidecar_free(s);
    /* silence: a second driver given id 0 stays quiet; a real id makes register writes */
    sidecar_t *q = sidecar_new_adlib(b, (size_t)n, opl, NULL);
    if (!q) return 1;
    sidecar_request(q, 0);                       /* id 0 silences the chip: its register clear is the request's own writes */
    w0 = writes;
    for (int t = 0; t < 200; t++) sidecar_tick(q);
    const unsigned quiet = writes - w0;
    sidecar_free(q);
    printf("id 0 for 200 ticks: %u writes\n", quiet);
    if (quiet != 0 || (id != 0 && made == 0)) { printf("FAILED\n"); return 1; }
    /* Run the real engine handler and timer with the second call argument.
     * Losing that word made every speed update reset the sidecar to zero. */
    uint64_t engine_hash[3];
    const uint16_t speeds[3] = { 0, 60, 120 };
    for (unsigned i = 0; i < 3; ++i) {
        q = sidecar_new_adlib(b, (size_t)n, opl, NULL);
        if (!q || !sidecar_request(q, 24) || !sidecar_request_arg(q, 23, speeds[i])) return 1;
        write_hash = UINT64_C(14695981039346656037); w0 = writes;
        for (int t = 0; t < 200; ++t) if (!sidecar_tick(q)) return 1;
        engine_hash[i] = write_hash;
        printf("engine %u knots: %u writes, hash %llx\n", speeds[i], writes - w0, (unsigned long long)write_hash);
        if (speeds[i]) {
            /* Ordinary throttle decrements do not themselves send command
             * 25. Ongoing nonzero airspeed updates must keep the voice alive. */
            for (int t = 0; t < 200; ++t)
                if (!sidecar_request_arg(q, 23, speeds[i]) || !sidecar_tick(q)) return 1;
            printf("continued speed updates at %u knots: %u active OPL voices\n", speeds[i], active_voices());
            if (!active_voices()) return 1;
            if (!sidecar_request(q, 25)) return 1; /* explicit engine-off command */
            for (int t = 0; t < 200; ++t) {
                if (!sidecar_request_arg(q, 23, speeds[i]) || !sidecar_tick(q)) return 1;
            }
            const unsigned active = active_voices();
            printf("explicit engine off at %u knots: %u active OPL voices\n", speeds[i], active);
            /* The original driver's engine-off command releases the voice;
             * keep that behavior rather than inventing an idle sound. */
            if (active) return 1;
        }
        sidecar_free(q);
    }
    if (engine_hash[0] == engine_hash[1] || engine_hash[1] == engine_hash[2]) return 1;
    free(b);
    printf("ok\n");
    return 0;
}
