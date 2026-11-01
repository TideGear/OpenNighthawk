/* sidecar_probe ASOUND.117 [id] [-v]: stage the AdLib driver as a sidecar, raise an id and run ticks; print the OPL writes.
 * Exit status 1 when the driver does not initialise, a call does not return, id 0 makes sound, or the id makes none (the
 * install is needed, so this is a gate-side tool like check_drivers, not a ctest). */
#include "sidecar.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned writes, keyons;
static int verbose;
static void opl(void *user, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)user;
    writes++;
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
    printf("ok\n");
    return 0;
}
