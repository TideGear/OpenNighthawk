/* fixes.c - the code fixes, as run-time overrides. See fixes.h. */
#include "fixes.h"
#include "recomp_rt.h"

/* GOG's VGAME.EXE: MicroProse's final 473.04 update. */
#define VGAME_47304 0x8287450CCA85106FULL

/* D5. VGAME's impact gate at 0x06D1C admits weapon types 1Eh, 1Dh and 1Ch;
 * anything else reaches the jmp at 0x6D2E to the skip at 0x6EBB. A supply
 * drop is type 26h, so the credit path at 0x6E4C - which tests for 26h
 * itself, at 0x6D35 - never runs. The fix lets 26h continue to 0x6D31 as
 * the three admitted types do; every other type still takes the jmp. */
static int fix_d5(machine_t *m)
{
    cpu_t *c = &m->cpu;
    const uint16_t type = seg_read16(c, c->seg[S_SS], (uint16_t)(c->r[R_BP] - 0x12));
    c->op_cs = c->seg[S_CS];
    c->op_ip = c->ip;
    c->ip = type == 0x26 ? 0x6D31 : 0x6EBB;
    c->icount++;                     /* the one instruction it replaces */
    return 1;
}

static const recomp_override FIXES[] = {
    { "D5", "VGAME.EXE", VGAME_47304, 0x0000, 0x6D2E, fix_d5,
      "supply drops earn their delivery credit" },
};

void fixes_register(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (unsigned i = 0; i < sizeof FIXES / sizeof FIXES[0]; i++) recomp_override_add(&FIXES[i]);
}

int fixes_enable(const char *id, int on)
{
    fixes_register();
    return recomp_override_enable(id, on);
}

void fixes_list(FILE *f)
{
    fixes_register();
    recomp_override_list(f);
}
