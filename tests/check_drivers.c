/* Staged driver-fix regression (Q15, Q16): check_drivers path/to/ASOUND.117 path/to/RSOUND.117.
 * The drivers are unmatched code, so the original instructions run in the
 * interpreter; each fix is stepped from its site with the fix off and on, and
 * the registers, memory and clock are compared with what the original
 * instructions give or with the stated correction. */
#include "fixes.h"
#include "matched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failures;
#undef CHECK
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); failures++; } } while(0)
enum { LOAD=0x1000, DATA=0x2000 };

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); *len = (size_t)n; return b;
}
/* One instruction as the run loop takes it: a fix hook first, else the original. */
static void step(void)
{
    cpu_t *c = &m.cpu;
    if (recomp_override_step(&m) > 0) return;
    CHECK(!cpu_step(c));
}
static void stage(const char *name, const uint8_t *file, size_t len, uint16_t code_base, uint16_t ip, const char *fix, int on)
{
    memset(m.mem, 0, MEM_SIZE); cpu_init(&m.cpu, m.mem); m.cpu.user = &m;
    m.engine = ENGINE_INTERP; m.ips = MACHINE_DEFAULT_IPS;
    fixes_enable("all", 0); fixes_enable(fix, on);
    recomp_module_load(NULL, &m, name, file, len, MODLOAD_OVERLAY, LOAD, 0);
    const size_t skip = (size_t)(file[8] | (file[9] << 8)) * 16u;
    memcpy(m.mem + ((uint32_t)LOAD << 4), file + skip, len - skip);
    m.cpu.seg[S_CS] = (uint16_t)(LOAD + code_base);
    m.cpu.seg[S_DS] = m.cpu.seg[S_SS] = LOAD;
    m.cpu.r[R_SP] = 0xF000; m.cpu.ip = ip; m.cpu.icount = 1000; m.cpu.stop_at = 100000;
}
static uint8_t get8(uint16_t at) { return m.mem[((uint32_t)LOAD << 4) + at]; }
static void put8(uint16_t at, uint8_t v) { m.mem[((uint32_t)LOAD << 4) + at] = v; }

static unsigned q15(const uint8_t *file, size_t len)
{
    unsigned checks = 0;
    /* [19DC] is generator A's count, [19FC] generator B's. AL then feeds the original AND and JE:
     * zero skips the take-over arm (0x0FB4), nonzero enters it (0x0F93). */
    const uint8_t pairs[][2] = { {0, 7}, {7, 0}, {3, 3}, {0, 0} };
    for (unsigned k = 0; k < sizeof pairs / sizeof pairs[0]; k++) for (int on = 0; on < 2; on++) {
        stage("ASOUND.117", file, len, 0x01B6, 0x0F8B, "Q15", on);
        put8(0x19DC, pairs[k][0]); put8(0x19FC, pairs[k][1]);
        m.cpu.r[R_AX] = 0xAB00;
        step();
        const uint8_t seen = on ? pairs[k][1] : pairs[k][0];
        CHECK(get_r8(&m.cpu, R_AL) == seen && get_r8(&m.cpu, R_AH) == 0xAB);
        CHECK(m.cpu.ip == 0x0F8E && m.cpu.icount == 1001);
        step(); step();                                  /* and ax,0xFF ; je */
        CHECK(m.cpu.ip == (seen ? 0x0F93 : 0x0FB4));
        CHECK(get8(0x19DC) == pairs[k][0] && get8(0x19FC) == pairs[k][1]);
        checks++;
    }
    return checks;
}

static unsigned q16(const uint8_t *file, size_t len)
{
    unsigned checks = 0;
    /* A record at DS:DATA: step bytes at +2 (volume) and +3 (pan), fields at +0Ch and +0Eh. */
    const struct { uint8_t field; int8_t step; } cases[] = {
        {0x40, 5}, {0x40, -5}, {0x7C, 8}, {0x7F, 1}, {0x03, -8}, {0x00, -1}, {0x7F, 0}, {0x00, 0}, {0x7E, 1}, {0x01, -1} };
    for (int pan = 0; pan < 2; pan++) for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) for (int on = 0; on < 2; on++) {
        const uint16_t site = pan ? 0x039D : 0x0348, next = pan ? 0x03A6 : 0x0351;
        const uint16_t step_at = DATA + (pan ? 3 : 2), field_at = DATA + (pan ? 0x0E : 0x0C);
        stage("RSOUND.117", file, len, 0x023E, site, "Q16", on);
        m.cpu.r[R_BX] = DATA; m.cpu.r[R_AX] = 0x5500;
        put8(step_at, (uint8_t)cases[k].step); put8(field_at, cases[k].field);
        const unsigned want_clock = 3;
        while (m.cpu.ip != next) step();
        const int sum = cases[k].field + cases[k].step;
        const uint8_t original = (uint8_t)sum;
        const uint8_t saturated = (uint8_t)(sum < 0 ? 0 : sum > 0x7F ? 0x7F : sum);
        const uint8_t want = on ? saturated : original;
        CHECK(get8(field_at) == want && get_r8(&m.cpu, R_AL) == want);
        CHECK(m.cpu.icount == 1000 + want_clock);
        CHECK(get8(step_at) == (uint8_t)cases[k].step && m.cpu.r[R_BX] == DATA);
        CHECK(on ? want <= 0x7F : 1);
        if (!on && sum >= 0 && sum <= 0x7F) CHECK(want == saturated);   /* in range: the fix changes nothing */
        checks++;
    }
    return checks;
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: check_drivers ASOUND.117 RSOUND.117\n"); return 2; }
    size_t alen, rlen;
    uint8_t *a = slurp(argv[1], &alen), *r = slurp(argv[2], &rlen);
    m.mem = calloc(1, MEM_SIZE);
    if (!a || !r || !m.mem) return 2;
    cpu_init(&m.cpu, m.mem); recomp_init(&m);
    const unsigned n15 = q15(a, alen), n16 = q16(r, rlen);
    recomp_shutdown(&m); free(m.mem); free(a); free(r);
    printf("Q15: %u staged runs; Q16: %u staged runs; both states; %d failures\n", n15, n16, failures);
    return failures ? 1 : 0;
}
