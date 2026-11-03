#include "sidecar.h"
#include "machine.h"
#include <stdlib.h>
#include <string.h>

enum { LOAD = 0x1000, STOP_SEG = 0x0F00, STACK_TOP = 0xF000, STEP_LIMIT = 2000000 };
/* ASOUND.117's entries as CS offsets (CS = LOAD + 01B6), read from a live run (docs/sound-board.md). */
static const uint16_t ADLIB_ENTRY[5] = { 0x0B76, 0x0B98, 0x0BB5, 0x0C08, 0x0C1C };
enum { E_INIT, E_REQUEST, E_RESYNC, E_TICK, E_NOISE };

struct sidecar {
    machine_t *m;
    sidecar_opl_fn opl;
    void *user;
    unsigned tick;
};

static void on_opl(void *user, uint64_t icount, uint8_t reg, uint8_t val)
{
    sidecar_t *s = user;
    if (s->opl) s->opl(s->user, icount, reg, val);
}

/* A far call to entry e. Requests carry the command and its optional value
 * above the return address, in the same stack order as the game. */
static int far_call(sidecar_t *s, int e, int has_arg, uint16_t arg, uint16_t value)
{
    cpu_t *c = &s->m->cpu;
    c->seg[S_SS] = c->seg[S_DS] = c->seg[S_ES] = LOAD;
    c->r[R_SP] = STACK_TOP;
    c->flags &= ~(uint32_t)F_DF;
    if (has_arg) { cpu_push16(c, value); cpu_push16(c, arg); }
    cpu_push16(c, STOP_SEG);       /* the far return address: CS, then IP */
    cpu_push16(c, 0);
    c->seg[S_CS] = (uint16_t)(LOAD + 0x01B6);
    c->ip = ADLIB_ENTRY[e];
    c->stop_at = ~(uint64_t)0;
    for (unsigned n = 0; n < STEP_LIMIT; n++) {
        if (c->seg[S_CS] == STOP_SEG && c->ip == 0) return 1;
        if (cpu_step(c)) return 0;
    }
    return 0;
}

sidecar_t *sidecar_new_adlib(const uint8_t *file, size_t len, sidecar_opl_fn opl, void *user)
{
    if (len < 0x40 || file[0] != 'M' || file[1] != 'Z') return NULL;
    const size_t hdr = (size_t)(file[8] | (file[9] << 8)) * 16u;
    const unsigned nrel = (unsigned)(file[6] | (file[7] << 8)), relat = (unsigned)(file[0x18] | (file[0x19] << 8));
    if (hdr >= len || len - hdr > 0x10000u + 0x10u || relat + nrel * 4u > len) return NULL;
    sidecar_t *s = calloc(1, sizeof *s);
    machine_t *m = calloc(1, sizeof *m);
    uint8_t *mem = calloc(1, MEM_SIZE);
    if (!s || !m || !mem) { free(s); free(m); free(mem); return NULL; }
    m->mem = mem;
    s->m = m; s->opl = opl; s->user = user;
    cpu_init(&m->cpu, m->mem);
    m->cpu.user = m;
    m->cpu.io_read = pc_io_read;
    m->cpu.io_write = pc_io_write;
    m->engine = ENGINE_INTERP;
    m->ips = MACHINE_DEFAULT_IPS;
    m->hooks.opl_write = on_opl;
    m->hooks.user = s;
    uint8_t *image = m->mem + ((uint32_t)LOAD << 4);
    memcpy(image, file + hdr, len - hdr);
    for (unsigned k = 0; k < nrel; k++) {                 /* each relocation adds the load segment to a word */
        const uint8_t *r = file + relat + k * 4u;
        const uint32_t at = (uint32_t)(r[2] | (r[3] << 8)) * 16u + (uint32_t)(r[0] | (r[1] << 8));
        if (at + 2 > 0x20000u) { sidecar_free(s); return NULL; }
        const uint16_t w = (uint16_t)(image[at] | (image[at + 1] << 8)) + LOAD;
        image[at] = (uint8_t)w; image[at + 1] = (uint8_t)(w >> 8);
    }
    m->cpu.icount = 1000;
    if (!far_call(s, E_INIT, 0, 0, 0)) { sidecar_free(s); return NULL; }
    return s;
}

void sidecar_free(sidecar_t *s)
{
    if (!s) return;
    free(s->m->mem);
    free(s->m);
    free(s);
}

int sidecar_request_arg(sidecar_t *s, uint16_t id, uint16_t value) { return far_call(s, E_REQUEST, 1, id, value); }
int sidecar_request(sidecar_t *s, uint16_t id) { return sidecar_request_arg(s, id, 0); }

int sidecar_tick(sidecar_t *s)
{
    int ok = 1;
    if (s->tick % 7 != 6) ok = far_call(s, E_TICK, 0, 0, 0);
    s->tick++;
    return far_call(s, E_NOISE, 0, 0, 0) && ok;
}

uint64_t sidecar_icount(const sidecar_t *s) { return s->m->cpu.icount; }
