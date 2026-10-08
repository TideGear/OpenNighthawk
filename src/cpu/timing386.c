/* timing386.c - the 386DX/33 timing profile's cost of one step (timing386.h).
 *
 * Follows 86Box's exec386_2386 (cpu/386.c:226-427) and prefetch_run (cpu/386_common.c:515-567) as
 * tools/ref86box/timing386.md describes them; the per-opcode figures are timing386_ops.h, generated
 * from tools/ref86box/ops386.json. Checked block by block against 86Box with
 * tools/ref86box/probe386.py. */
#include "timing386.h"
#include "x86_sem.h"

#include "timing386_ops.h"

void t386_enable(cpu_t *c, int mem_wait, uint32_t vga_lo, uint32_t vga_size, uint32_t vga_byte)
{
    c->t386 = 1;
    c->t386_mem = mem_wait;
    c->t386_pf_bytes = c->t386_pf_prefixes = 0;
    c->t386_chunk = -1;
    c->t386_chunk_n = 0;
    c->t386_chunk_held = 0;
    c->t386_vga_lo = vga_lo;
    c->t386_vga_size = vga_size;
    c->t386_vga_byte = vga_byte;
}

/* The prefetch queue (386_common.c:515-567). */
static uint32_t prefetch_run(cpu_t *c, int instr, int bytes, int modrm, int reads, int writes)
{
    const int w = c->t386_mem;              /* cpu_prefetch_cycles = cpu_cycles_read = write */
    const int mem = (reads + writes) * w;
    uint32_t stall = 0;
    if (instr < mem) instr = mem;
    c->t386_pf_bytes -= c->t386_pf_prefixes + bytes;
    if (modrm >= 0) {
        if ((modrm & 0xC7) == 0x06) c->t386_pf_bytes -= 2;
        else if ((modrm & 0xC0) != 0xC0) c->t386_pf_bytes -= (modrm & 0xC0) >> 6;
    }
    while (c->t386_pf_bytes < 0) { c->t386_pf_bytes += 4; stall += (uint32_t)w; }
    instr -= mem;
    while (instr >= w) { c->t386_pf_bytes += 4; instr -= w; }
    c->t386_pf_prefixes = 0;
    if (c->t386_pf_bytes > 16) c->t386_pf_bytes = 16;
    return stall;
}

static uint32_t run_entry(cpu_t *c, const t386_pf_t *p, int base, int elapsed)
{
    if (!p->has) return 0;
    const int instr = p->instr == -1 ? base : p->instr == -2 ? elapsed : p->instr;
    return prefetch_run(c, instr, p->bytes, p->modrm ? c->t386_modrm : -1,
                        p->reads + p->reads_l, p->writes + p->writes_l);
}

/* A string instruction under REP. 86Box runs elements in one dispatch until more than 100 cycles
 * are used (device cycles included), then re-fetches the instruction: the prefixes are charged
 * again and the queue is run once for the chunk (x86_ops_rep_2386.h). Here every element is a step;
 * the chunk's cycles are held and land on the clock when it ends, with interrupts kept out until
 * then, as in 86Box. CMPS, INS and OUTS do one element a dispatch; SCAS chunks like MOVS. */
static uint32_t rep_step(cpu_t *c, uint8_t op, const t386_op_t *e, int prefix_cycles, int npfx)
{
    const int one_each = (op == 0xA6 || op == 0xA7 || (op >= 0x6C && op <= 0x6F));
    const int again = c->ip == c->op_ip && c->seg[S_CS] == c->op_cs;
    const int reads = (op == 0xA4 || op == 0xA5 || op == 0xA6 || op == 0xA7 || op == 0xAC ||
                       op == 0xAD || op == 0xAE || op == 0xAF || op == 0x6E || op == 0x6F);
    const int writes = (op == 0xA4 || op == 0xA5 || op == 0xAA || op == 0xAB || op == 0x6C || op == 0x6D);
    if (c->t386_chunk < 0) {
        /* The dispatch's prefixes. A CX of 0 runs no element: the prefix and a queue run only. */
        c->t386_pf_prefixes += npfx;
        c->t386_chunk_held = (uint32_t)prefix_cycles;
        c->t386_chunk = 0;
        c->t386_chunk_n = 0;
        if (!c->t386_elem) {
            uint32_t t = c->t386_chunk_held + prefetch_run(c, 0, 1, -1, 0, 0);
            c->t386_chunk = -1;
            return t;
        }
    }
    c->t386_chunk += e->per + (int)c->t386_dev;
    c->t386_chunk_held += (uint32_t)e->per + c->t386_dev;
    c->t386_chunk_n++;
    if (again && !one_each && c->t386_chunk <= 100) {
        c->inhibit_at = c->icount;           /* no interrupt inside a chunk */
        return 0;
    }
    const int n = c->t386_chunk_n;
    uint32_t t = c->t386_chunk_held + prefetch_run(c, e->per * n, 1, -1, reads ? n : 0, writes ? n : 0);
    c->t386_chunk = -1;
    return t;
}

uint32_t t386_step(cpu_t *c, uint8_t op, int stop)
{
    const int npfx = c->t386_seg_pfx + c->t386_rep_pfx + c->t386_lock_pfx;
    const int prefix_cycles = 4 * c->t386_seg_pfx + 2 * c->t386_rep_pfx + 4 * c->t386_lock_pfx;
    const int mem_form = c->t386_modrm >= 0 && (c->t386_modrm & 0xC0) != 0xC0;
    const t386_op_t *e;
    uint32_t total;

    if (stop) {                              /* a divide error or other fault: x86_int's 70 */
        c->t386_chunk = -1;
        return 70 + (uint32_t)prefix_cycles + c->t386_dev;
    }
    if (c->t386_rep_pfx && ((op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF) ||
                            (op >= 0x6C && op <= 0x6F))) {
        return rep_step(c, op, &t386_rep[op], prefix_cycles, npfx);
    }
    c->t386_chunk = -1;
    e = &t386_one[op][c->t386_modrm >= 0 ? (c->t386_modrm >> 3) & 7 : 0];
    int base = e->cycles[mem_form];
    const t386_pf_t *p = &e->pf[mem_form];
    int taken = 0;

    /* Taken or not from the condition, not from IP: a jump of 0 lands where it would fall through.
     * The jumps leave the flags alone and LOOP has already counted CX down. */
    switch (e->kind) {
    case T386_JCC:
        taken = x86_cond(c, op & 0xF);
        if (taken) base += 4;
        break;
    case T386_LOOP: {
        const int zf = (c->flags & F_ZF) != 0;
        taken = c->r[R_CX] != 0 && (op == 0xE2 || (op == 0xE1 ? zf : !zf));
        break;
    }
    case T386_JCXZ:
        taken = c->r[R_CX] == 0;
        if (taken) base += 4;
        break;
    case T386_SHIFTN: {
        /* A count of 0 returns before any charge; the prefixes carry to the next instruction. */
        uint8_t n = (op == 0xD2 || op == 0xD3) ? (uint8_t)(c->r[R_CX] & 0xFF)
                    : mem_read8(c, phys(c->seg[S_CS], (uint16_t)(c->ip - 1)));
        if ((n & 31) == 0) {
            c->t386_pf_prefixes += npfx;
            return (uint32_t)prefix_cycles + c->t386_dev;
        }
        break;
    }
    case T386_INTO:
        if (c->seg[S_CS] != c->op_cs || c->ip != (uint16_t)(c->op_ip + npfx + 1)) {
            base = 37;                       /* x86_int_sw: no flush */
            c->t386_pf_prefixes += npfx;
            total = (uint32_t)(prefix_cycles + base) + c->t386_dev;
            return total + prefetch_run(c, base + (int)c->t386_dev, 1, -1, 0, 0);
        }
        break;
    case T386_ENTER: {
        const uint8_t level = mem_read8(c, phys(c->seg[S_CS], (uint16_t)(c->ip - 1)));
        int reads = 0, writes = 1;
        base = 10;
        if (level) { base += 4 * (level - 1) + 5; reads = level - 1; writes += level; }
        c->t386_pf_prefixes += npfx;
        total = (uint32_t)(prefix_cycles + base) + c->t386_dev;
        return total + prefetch_run(c, base, 3, -1, reads, writes);
    }
    case T386_HLT:
    case T386_UD:
    case T386_NOTIMED:
    case T386_NORMAL:
    default:
        break;
    }
    c->t386_pf_prefixes += npfx;
    total = (uint32_t)(prefix_cycles + base) + c->t386_dev;
    if (e->kind == T386_JCC || e->kind == T386_JCXZ) {
        /* PREFETCH_RUN(3 or 7 / 5 or 9, ...) */
        total += prefetch_run(c, base, p->bytes, -1, 0, 0);
    } else {
        total += run_entry(c, p, base, base + (int)c->t386_dev);
    }
    if (e->flush == 1 || (e->flush == 2 && taken)) c->t386_pf_bytes = 0;
    return total;
}
