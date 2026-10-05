/* dos_memory.c - extracted DOS/BIOS services; see dos.c for provenance. */
#include "dos_internal.h"

/* ===================================================================== */
/* Memory allocation                                                     */
/* ===================================================================== */
/* Memory control blocks in guest memory, as DOS keeps them: a chain of
 * 16-byte headers ('M', or 'Z' for the last; the owner's PSP, 0 when free;
 * the size in paragraphs; at +8 the owner's name), each followed by its
 * block. The algorithms are DOSBox 0.74's (src/dos/dos_memory.cpp, GPL-2
 * or later), the DOS the game is played on: first fit by default, free
 * neighbours merged before each search, a shrink that leaves the freed tail
 * as its own block until the next search. The answers a program gets - the
 * segment, the largest free block, the error - follow from the same chain
 * and the same steps. */

#define MCB_FREE 0

static uint8_t  mcb_type(machine_t *m, uint16_t s)  { return mem_read8(&m->cpu, (uint32_t)s * 16u); }
static uint16_t mcb_owner(machine_t *m, uint16_t s) { return mem_read16(&m->cpu, (uint32_t)s * 16u + 1); }
static uint16_t mcb_size(machine_t *m, uint16_t s)  { return mem_read16(&m->cpu, (uint32_t)s * 16u + 3); }
void dos_mcb_set_type(machine_t *m, uint16_t s, uint8_t t)   { mem_write8(&m->cpu, (uint32_t)s * 16u, t); }
void dos_mcb_set_owner(machine_t *m, uint16_t s, uint16_t o) { mem_write16(&m->cpu, (uint32_t)s * 16u + 1, o); }
void dos_mcb_set_size(machine_t *m, uint16_t s, uint16_t n)  { mem_write16(&m->cpu, (uint32_t)s * 16u + 3, n); }

static void mcb_get_name(machine_t *m, uint16_t s, uint8_t name[8])
{
    for (int i = 0; i < 8; i++) name[i] = mem_read8(&m->cpu, (uint32_t)s * 16u + 8 + (uint32_t)i);
}

void dos_mcb_set_name(machine_t *m, uint16_t s, const uint8_t name[8])
{
    for (int i = 0; i < 8; i++) mem_write8(&m->cpu, (uint32_t)s * 16u + 8 + (uint32_t)i, name[i]);
}


static void mem_compress(machine_t *m)
{
    uint16_t s = m->first_mcb;
    for (int guard = 0; guard < 0x10000 && mcb_type(m, s) != 'Z'; guard++) {
        uint16_t next = (uint16_t)(s + mcb_size(m, s) + 1);
        if (mcb_owner(m, s) == MCB_FREE && mcb_owner(m, next) == MCB_FREE) {
            dos_mcb_set_size(m, s, (uint16_t)(mcb_size(m, s) + mcb_size(m, next) + 1));
            dos_mcb_set_type(m, s, mcb_type(m, next));
        } else {
            s = next;
        }
    }
}

/* DOS_AllocateMemory. On failure *paras is the largest free block. */
int dos_mem_alloc(machine_t *m, uint16_t *seg_out, uint16_t *paras)
{
    mem_compress(m);
    const uint16_t want = *paras, strat = (uint16_t)(m->alloc_strategy & 0x3F);
    const uint16_t me = dos_current_psp(m);
    uint8_t name[8];
    mcb_get_name(m, (uint16_t)(me - 1), name);
    uint16_t big = 0, found = 0, found_size = 0;
    uint16_t s = m->first_mcb;
    for (int guard = 0; guard < 0x10000; guard++) {
        if (mcb_owner(m, s) == MCB_FREE) {
            const uint16_t size = mcb_size(m, s);
            if (size < want) {
                if (big < size) big = size;
            } else if (size == want && strat < 2) {
                dos_mcb_set_owner(m, s, me);
                *seg_out = (uint16_t)(s + 1);
                return 1;
            } else if (strat == 0) {                    /* first fit */
                const uint16_t next = (uint16_t)(s + want + 1);
                dos_mcb_set_owner(m, next, MCB_FREE);
                dos_mcb_set_type(m, next, mcb_type(m, s));
                dos_mcb_set_size(m, next, (uint16_t)(size - want - 1));
                dos_mcb_set_size(m, s, want);
                dos_mcb_set_type(m, s, 'M');
                dos_mcb_set_owner(m, s, me);
                dos_mcb_set_name(m, s, name);
                *seg_out = (uint16_t)(s + 1);
                return 1;
            } else if (strat == 1) {                    /* best fit: note the smallest */
                if (!found_size || size < found_size) { found = s; found_size = size; }
            } else {                                    /* last fit: note the last */
                found = s; found_size = size;
            }
        }
        if (mcb_type(m, s) == 'Z') break;
        s = (uint16_t)(s + mcb_size(m, s) + 1);
    }
    if (found) {
        if (strat == 1) {
            const uint16_t next = (uint16_t)(found + want + 1);
            dos_mcb_set_owner(m, next, MCB_FREE);
            dos_mcb_set_type(m, next, mcb_type(m, found));
            dos_mcb_set_size(m, next, (uint16_t)(found_size - want - 1));
            dos_mcb_set_size(m, found, want);
            dos_mcb_set_type(m, found, 'M');
            dos_mcb_set_owner(m, found, me);
            dos_mcb_set_name(m, found, name);
            *seg_out = (uint16_t)(found + 1);
        } else if (found_size == want) {
            dos_mcb_set_owner(m, found, me);
            dos_mcb_set_name(m, found, name);
            *seg_out = (uint16_t)(found + 1);
        } else {
            *seg_out = (uint16_t)(found + 1 + found_size - want);
            const uint16_t blk = (uint16_t)(*seg_out - 1);
            dos_mcb_set_size(m, blk, want);
            dos_mcb_set_type(m, blk, mcb_type(m, found));
            dos_mcb_set_owner(m, blk, me);
            dos_mcb_set_name(m, blk, name);
            dos_mcb_set_size(m, found, (uint16_t)(found_size - want - 1));
            dos_mcb_set_owner(m, found, MCB_FREE);
            dos_mcb_set_type(m, found, 'M');
        }
        return 1;
    }
    *paras = big;
    return 0;
}

/* DOS_ResizeMemory. Returns 0, or the error with *paras the most possible. */
uint16_t dos_mem_resize(machine_t *m, uint16_t seg, uint16_t *paras)
{
    const uint16_t s = (uint16_t)(seg - 1);
    if (mcb_type(m, s) != 'M' && mcb_type(m, s) != 'Z') return 7;     /* MCB destroyed */
    mem_compress(m);
    const uint16_t me = dos_current_psp(m);
    uint16_t total = mcb_size(m, s);
    const uint16_t next = (uint16_t)(seg + total);
    if (*paras <= total) {
        if (*paras == total) return 0;
        const uint16_t nn = (uint16_t)(seg + *paras);
        dos_mcb_set_size(m, s, *paras);
        dos_mcb_set_type(m, nn, mcb_type(m, s));
        if (mcb_type(m, s) == 'Z') dos_mcb_set_type(m, s, 'M');
        dos_mcb_set_size(m, nn, (uint16_t)(total - *paras - 1));
        dos_mcb_set_owner(m, nn, MCB_FREE);
        dos_mcb_set_owner(m, s, me);
        return 0;
    }
    if (mcb_type(m, s) != 'Z' && mcb_owner(m, next) == MCB_FREE)
        total = (uint16_t)(total + mcb_size(m, next) + 1);
    if (*paras < total) {
        if (mcb_type(m, s) != 'Z') dos_mcb_set_type(m, s, mcb_type(m, next));
        dos_mcb_set_size(m, s, *paras);
        const uint16_t nn = (uint16_t)(seg + *paras);
        dos_mcb_set_size(m, nn, (uint16_t)(total - *paras - 1));
        dos_mcb_set_type(m, nn, mcb_type(m, s));
        dos_mcb_set_owner(m, nn, MCB_FREE);
        dos_mcb_set_type(m, s, 'M');
        dos_mcb_set_owner(m, s, me);
        return 0;
    }
    if (mcb_owner(m, next) == MCB_FREE && mcb_type(m, s) != 'Z')
        dos_mcb_set_type(m, s, mcb_type(m, next));
    dos_mcb_set_size(m, s, total);
    dos_mcb_set_owner(m, s, me);
    if (*paras == total) return 0;
    *paras = total;
    return ERR_NO_MEMORY;
}

/* DOS_FreeMemory: 0 or the error. */
uint16_t dos_mem_free(machine_t *m, uint16_t seg)
{
    if (seg < m->first_mcb + 1) return 9;                      /* invalid block */
    const uint16_t s = (uint16_t)(seg - 1);
    if (mcb_type(m, s) != 'M' && mcb_type(m, s) != 'Z') return 9;
    dos_mcb_set_owner(m, s, MCB_FREE);
    return 0;
}

/* DOS_FreeProcessMemory. */
void dos_mem_free_process(machine_t *m, uint16_t psp)
{
    uint16_t s = m->first_mcb;
    for (int guard = 0; guard < 0x10000; guard++) {
        if (mcb_owner(m, s) == psp) dos_mcb_set_owner(m, s, MCB_FREE);
        if (mcb_type(m, s) == 'Z') break;
        s = (uint16_t)(s + mcb_size(m, s) + 1);
    }
    mem_compress(m);
}

uint16_t dos_block_end(machine_t *m, uint16_t seg)
{
    uint16_t s = m->first_mcb;
    for (int guard = 0; guard < 0x10000; guard++) {
        const uint16_t end = (uint16_t)(s + 1 + mcb_size(m, s));
        if (seg > s && seg < end) return end;
        if (mcb_type(m, s) == 'Z') break;
        s = end;
    }
    return 0;
}

