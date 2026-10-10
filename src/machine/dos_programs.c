/* dos_programs.c - extracted DOS/BIOS services; see dos.c for provenance. */
#include "dos_internal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "timing386.h"
#include "x86_sem.h"

/* ===================================================================== */
/* Program loading                                                       */
/* ===================================================================== */

/* The 386 profile's cost of an EXEC or overlay load (timing386.h): by which of the three
 * measured sizes a file is closest to, not a per-byte formula (see the constants' comment). */
static void t386_exec(machine_t *m, long fsz)
{
    t386_file(m, fsz <= 1024 ? T386_EXEC_SMALL : fsz <= 9506 ? T386_EXEC_MEDIUM : T386_EXEC_LARGE);
}

/* A new PSP, as DOSBox's DOS_PSP::MakeNew and SetupPSP build one: the
 * INT 20h and the far call to DOS, the top of the block, the parent, the
 * INT 22h/23h/24h vectors as they are now, the handle table copied from
 * the parent (each inherited entry referenced once more), the DOS version
 * DOS will report, the environment. */
static void psp_make(machine_t *m, uint16_t psp, uint16_t memsize, uint16_t parent, uint16_t env)
{
    cpu_t *c = &m->cpu;
    for (int i = 0; i < 0x100; i++) mem_write8(c, phys(psp, (uint16_t)i), 0);
    mem_write8(c, phys(psp, 0x00), 0xCD);           /* INT 20h */
    mem_write8(c, phys(psp, 0x01), 0x20);
    mem_write16(c, phys(psp, 0x02), (uint16_t)(psp + memsize));
    mem_write8(c, phys(psp, 0x05), 0xEA);           /* far call to DOS (CP/M) */
    mem_write16(c, phys(psp, 0x06), 0xFFFF);
    mem_write16(c, phys(psp, 0x08), 0xDEAD);
    for (int v = 0; v < 3; v++) {                   /* INT 22h, 23h, 24h */
        mem_write16(c, phys(psp, (uint16_t)(0x0A + 4 * v)), mem_read16(c, (uint32_t)(0x22 + v) * 4));
        mem_write16(c, phys(psp, (uint16_t)(0x0C + 4 * v)), mem_read16(c, (uint32_t)(0x22 + v) * 4 + 2));
    }
    mem_write16(c, phys(psp, 0x16), parent);
    mem_write16(c, phys(psp, 0x2C), env);
    mem_write16(c, phys(psp, 0x32), 20);
    mem_write16(c, phys(psp, 0x34), 0x18);
    mem_write16(c, phys(psp, 0x36), psp);
    mem_write16(c, phys(psp, 0x38), 0xFFFF);
    mem_write16(c, phys(psp, 0x3A), 0xFFFF);
    mem_write16(c, phys(psp, 0x40), 0x0005);
    mem_write8(c, phys(psp, 0x50), 0xCD);           /* INT 21h ; RETF */
    mem_write8(c, phys(psp, 0x51), 0x21);
    mem_write8(c, phys(psp, 0x52), 0xCB);
    for (int i = 0; i < 20; i++) {
        uint8_t h = parent ? mem_read8(c, phys(parent, (uint16_t)(0x18 + i))) : 0xFF;
        if (h != 0xFF && h < sizeof m->sft_ref) m->sft_ref[h]++;
        mem_write8(c, phys(psp, (uint16_t)(0x18 + i)), h);
    }
}

/* The environment a child gets (DOSBox MakeEnv): the given one, or the
 * parent's, copied up to its double zero, then the word 1 and the
 * program's full name; the block is that plus 83 bytes, in paragraphs. */
static uint16_t make_env(machine_t *m, uint16_t from, const char *fullname, uint16_t *env_out)
{
    cpu_t *c = &m->cpu;
    uint16_t size = 0;
    if (from) {
        while (mem_read16(c, phys(from, size)) != 0) {
            if (++size >= 0x8000u - 83u) return 10;        /* environment invalid */
        }
        size = (uint16_t)(size + 2);
    } else {
        size = 1;
    }
    const uint32_t bytes = (uint32_t)size + 83u;
    uint16_t paras = (uint16_t)((bytes >> 4) + ((bytes & 15) ? 1 : 0));
    uint16_t seg = 0;
    if (!dos_mem_alloc(m, &seg, &paras)) return ERR_NO_MEMORY;
    uint16_t o = 0;
    if (from) for (; o < size; o++) mem_write8(c, phys(seg, o), mem_read8(c, phys(from, o)));
    else mem_write8(c, phys(seg, o++), 0);
    mem_write16(c, phys(seg, o), 1);
    o = (uint16_t)(o + 2);
    for (const char *p = fullname; ; p++) {
        mem_write8(c, phys(seg, o++), (uint8_t)*p);
        if (!*p) break;
    }
    *env_out = seg;
    return 0;
}

/* The name DOS records in the program's MCB: the file name without its
 * extension, upper case, zero-padded. */
static void mcb_program_name(const char *path, uint8_t out[8])
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == ':' || *p == '\\' || *p == '/') b = p + 1;
    memset(out, 0, 8);
    for (int i = 0; i < 8 && b[i] && b[i] != '.'; i++) out[i] = (uint8_t)toupper((unsigned char)b[i]);
}

/* EXEC (load and go), as DOSBox's DOS_Execute does it: the environment,
 * then the largest block or what the header asks for, the image at PSP+10h
 * (or at the top of the block when the header asks for no memory), the
 * PSP, the command tail and FCBs from the parameter block, INT 22h set to
 * the caller's return address, and the program entered with the registers
 * DOSBox gives it. ret_cs:ret_ip is where the caller continues. */
uint16_t dos_load_program(machine_t *m, const char *name, const exec_params *ep,
                             uint16_t ret_cs, uint16_t ret_ip, uint16_t *psp_out)
{
    cpu_t *c = &m->cpu;
    long fsz = 0;
    uint8_t *raw = dos_read_whole(m, dos_basename(name), &fsz);
    if (!raw) return ERR_FILE_NOT_FOUND;
    if (fsz == 0) { free(raw); return ERR_ACCESS_DENIED; }
    t386_exec(m, fsz);

    int is_mz = (fsz >= 28 && ((raw[0] == 'M' && raw[1] == 'Z') || (raw[0] == 'Z' && raw[1] == 'M')));
    uint16_t hdr[14] = {0};
    uint32_t hdr_size = 0, image = 0;
    if (is_mz) {
        memcpy(hdr, raw, sizeof(hdr));
        const uint32_t pages = hdr[2] & 0x07FFu;
        hdr_size = (uint32_t)hdr[4] * 16u;
        image = pages * 512u - hdr_size;
        if (image + hdr_size < 512u) image = 512u - hdr_size;
    }

    /* The full name, as DOS canonicalises it, for the environment. */
    char fullname[600];
    {
        const char *b = name;
        if (b[0] && b[1] == ':') b += 2;
        while (*b == '\\' || *b == '/') b++;
        snprintf(fullname, sizeof fullname, "C:\\%s", b);
        for (char *p = fullname; *p; p++) { *p = (char)toupper((unsigned char)*p); if (*p == '/') *p = '\\'; }
    }

    uint16_t env = 0;
    uint16_t err = make_env(m, ep->env_seg ? ep->env_seg : ep->parent_env, fullname, &env);
    if (err) { free(raw); return err; }

    uint16_t maxfree = 0xFFFF, dummy = 0;
    dos_mem_alloc(m, &dummy, &maxfree);
    uint16_t minsize, maxsize;
    if (!is_mz) {
        minsize = 0x1000; maxsize = 0xFFFF;
    } else {
        uint32_t lo = image + (uint32_t)hdr[5] * 16u + 256u;
        minsize = lo > 0xFFFF0u ? 0xFFFF : (uint16_t)((lo >> 4) + ((lo & 15) ? 1 : 0));
        if (hdr[6]) {
            uint32_t hi = image + (uint32_t)hdr[6] * 16u + 256u;
            maxsize = hi > 0xFFFF0u ? 0xFFFF : (uint16_t)((hi >> 4) + ((hi & 15) ? 1 : 0));
        } else {
            maxsize = 0xFFFF;
        }
    }
    if (maxfree < minsize) {
        if (!is_mz && fsz < 0xF800) minsize = (uint16_t)(((fsz + 0x10) >> 4) + 0x20);
        if (maxfree < minsize) { dos_mem_free(m, env); free(raw); return ERR_NO_MEMORY; }
    }
    uint16_t memsize = maxfree < maxsize ? maxfree : maxsize;
    uint16_t psp = 0;
    if (!dos_mem_alloc(m, &psp, &memsize)) { dos_mem_free(m, env); free(raw); return ERR_NO_MEMORY; }
    uint16_t load_seg = (uint16_t)(psp + 0x10);
    if (is_mz && hdr[5] == 0 && hdr[6] == 0)
        load_seg = (uint16_t)((((uint32_t)psp + memsize) * 16u - image) / 16u);

    /* Announced before the image is written, so whatever this load
     * replaces is forgotten before its bytes change. */
    if (m->hooks.module_load)
        m->hooks.module_load(m->hooks.user, m, dos_basename(name), raw, (size_t)fsz,
                             is_mz ? MODLOAD_EXEC : MODLOAD_COM,
                             is_mz ? load_seg : psp, is_mz ? load_seg : psp);

    if (is_mz) {
        uint32_t avail = (uint32_t)fsz > hdr_size ? (uint32_t)fsz - hdr_size : 0;
        dos_guest_write(c, phys(load_seg, 0), raw + hdr_size, avail < image ? avail : image);
        uint16_t nreloc = hdr[3], reloc_off = hdr[12];
        for (unsigned i = 0; i < nreloc; i++) {
            uint16_t ro, rs;
            memcpy(&ro, raw + reloc_off + i * 4, 2);
            memcpy(&rs, raw + reloc_off + i * 4 + 2, 2);
            uint16_t s = (uint16_t)(load_seg + rs);
            seg_write16(c, s, ro, (uint16_t)(seg_read16(c, s, ro) + load_seg));
        }
    } else {
        dos_guest_write(c, phys(psp, 0x100), raw, (size_t)(fsz < 0xFEFF ? fsz : 0xFEFF));
    }

    /* The PSP and both blocks' owner; INT 22h at the caller's return. */
    dos_mcb_set_owner(m, (uint16_t)(psp - 1), psp);
    dos_mcb_set_owner(m, (uint16_t)(env - 1), psp);
    psp_make(m, psp, memsize, ep->parent_psp, env);
    if (ep->cmd_seg || ep->cmd_off) {
        for (int i = 0; i < 128; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x80 + i)), mem_read8(c, phys(ep->cmd_seg, (uint16_t)(ep->cmd_off + i))));
    } else {
        mem_write8(c, phys(psp, 0x80), 0);
        mem_write8(c, phys(psp, 0x81), 0x0D);
    }
    mem_write16(c, 0x22 * 4, ret_ip);
    mem_write16(c, 0x22 * 4 + 2, ret_cs);
    for (int v = 0; v < 3; v++) {
        mem_write16(c, phys(psp, (uint16_t)(0x0A + 4 * v)), mem_read16(c, (uint32_t)(0x22 + v) * 4));
        mem_write16(c, phys(psp, (uint16_t)(0x0C + 4 * v)), mem_read16(c, (uint32_t)(0x22 + v) * 4 + 2));
    }
    if (ep->fcb1_seg || ep->fcb1_off)
        for (int i = 0; i < 16; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x5C + i)), mem_read8(c, phys(ep->fcb1_seg, (uint16_t)(ep->fcb1_off + i))));
    if (ep->fcb2_seg || ep->fcb2_off)
        for (int i = 0; i < 16; i++)
            mem_write8(c, phys(psp, (uint16_t)(0x6C + i)), mem_read8(c, phys(ep->fcb2_seg, (uint16_t)(ep->fcb2_off + i))));
    m->dta = phys(psp, 0x80);
    uint8_t nm[8];
    mcb_program_name(name, nm);
    dos_mcb_set_name(m, (uint16_t)(psp - 1), nm);

    if (is_mz) {
        c->seg[S_SS] = (uint16_t)(load_seg + hdr[7]);
        c->r[R_SP]   = hdr[8];
        c->seg[S_CS] = (uint16_t)(load_seg + hdr[11]);
        c->ip        = hdr[10];
    } else {
        c->seg[S_SS] = psp;
        c->r[R_SP]   = 0xFFFE;
        seg_write16(c, psp, 0xFFFE, 0);
        c->seg[S_CS] = psp;
        c->ip        = 0x100;
    }
    /* DOSBox's registers at entry; the caller's flags keep only what is
     * not an arithmetic flag, with interrupts on and no trap. */
    c->seg[S_DS] = c->seg[S_ES] = psp;
    c->r[R_AX] = c->r[R_BX] = 0;
    c->r[R_CX] = 0x00FF;
    c->r[R_DX] = psp;
    c->r[R_SI] = c->ip;
    c->r[R_DI] = c->r[R_SP];
    c->r[R_BP] = 0x091C;
    c->flags = (uint16_t)((ep->flags & (uint16_t)(F_DF)) | cpu_flags_fixed(c) | F_IF);
    cpu_irq_state_changed(c);

    dos_log(m, "[exec] %-14s %s  psp=%04X env=%04X load=%04X..%04X (%u paras)  entry %04X:%04X @%llu\n",
            name, is_mz ? "MZ " : "COM", psp, env, load_seg, (unsigned)(psp + memsize), memsize,
            c->seg[S_CS], c->ip, (unsigned long long)c->icount);
    free(raw);
    *psp_out = psp;
    return 0;
}

/* INT 21h/4B03: an MZ image at a caller-chosen segment, no PSP, no
 * allocation, no transfer of control. How the shell brings in MISC.EXE,
 * the graphics driver and the sound driver. */
uint16_t dos_load_overlay(machine_t *m, const char *name, uint16_t load_seg,
                             uint16_t reloc_factor)
{
    cpu_t *c = &m->cpu;
    long fsz = 0;
    uint8_t *raw = dos_read_whole(m, dos_basename(name), &fsz);
    if (!raw) return ERR_FILE_NOT_FOUND;
    t386_exec(m, fsz);

    uint32_t body;
    unsigned nreloc = 0;
    if (m->hooks.module_load)
        m->hooks.module_load(m->hooks.user, m, dos_basename(name), raw, (size_t)fsz,
                             MODLOAD_OVERLAY, load_seg, reloc_factor);
    if (fsz >= 28 && raw[0] == 'M' && raw[1] == 'Z') {
        uint16_t hdr[14];
        memcpy(hdr, raw, sizeof(hdr));
        uint32_t hdr_size = (uint32_t)hdr[4] * 16;
        uint32_t img_size = hdr[2] ? (uint32_t)(hdr[2] - 1) * 512 + (hdr[1] ? hdr[1] : 512) : 0;
        if (img_size > (uint32_t)fsz) img_size = (uint32_t)fsz;
        if (img_size < hdr_size) { free(raw); return ERR_BAD_FORMAT; }
        body = img_size - hdr_size;
        dos_guest_write(c, phys(load_seg, 0), raw + hdr_size, body);
        nreloc = hdr[3];
        for (unsigned i = 0; i < nreloc; i++) {
            uint16_t ro, rs;
            memcpy(&ro, raw + hdr[12] + i * 4, 2);
            memcpy(&rs, raw + hdr[12] + i * 4 + 2, 2);
            uint16_t s = (uint16_t)(load_seg + rs);
            seg_write16(c, s, ro, (uint16_t)(seg_read16(c, s, ro) + reloc_factor));
        }
    } else {
        body = (uint32_t)fsz;
        dos_guest_write(c, phys(load_seg, 0), raw, body);
    }
    dos_log(m, "[overlay] %-12s -> %04X (%u bytes, %u relocations, factor %04X) @%llu\n",
            name, load_seg, body, nreloc, reloc_factor, (unsigned long long)c->icount);
    free(raw);
    return 0;
}

int dos_terminate(machine_t *m, uint8_t code)
{
    cpu_t *c = &m->cpu;
    uint16_t psp = dos_current_psp(m);
    /* The process's handles: its own files close; inherited entries drop
     * their reference (DOSBox DOS_PSP::CloseFiles). */
    dos_close_files_of(m, psp);
    for (int i = 0; i < 20 && psp; i++) {
        uint8_t h = mem_read8(c, phys(psp, (uint16_t)(0x18 + i)));
        if (h != 0xFF) { dos_sft_release(m, h); mem_write8(c, phys(psp, (uint16_t)(0x18 + i)), 0xFF); }
    }
    m->last_child_exit = code;
    m->return_mode = 0;

    if (m->nproc <= 1) {
        dos_log(m, "[exit] %s terminated with code %d (root)\n", dos_current_program(m), code);
        dos_mem_free_process(m, psp);
        m->exited = 1;
        m->exit_code = code;
        c->stop_at = 0;
        return 1;
    }

    dos_proc *p = &m->procs[m->nproc - 1];
    dos_log(m, "[exit] %s terminated with code %d -> back to %s @%llu\n",
            p->name, code, m->procs[m->nproc - 2].name, (unsigned long long)c->icount);

    /* DOSBox's DOS_Terminate: the return address from the PSP's INT 22h
     * entry, the vectors 22h-24h put back as the PSP saved them, the
     * parent's registers exactly as they were at its EXEC call, and the
     * flags DOSBox writes (7202h: on this 286, 0202h), then the process's
     * memory freed. */
    const uint16_t ret_ip = mem_read16(c, phys(psp, 0x0A)), ret_cs = mem_read16(c, phys(psp, 0x0C));
    for (int v = 0; v < 3; v++) {
        mem_write16(c, (uint32_t)(0x22 + v) * 4, mem_read16(c, phys(psp, (uint16_t)(0x0A + 4 * v))));
        mem_write16(c, (uint32_t)(0x22 + v) * 4 + 2, mem_read16(c, phys(psp, (uint16_t)(0x0C + 4 * v))));
    }
    memcpy(c->r, p->r, sizeof(c->r));
    memcpy(c->seg, p->seg, sizeof(c->seg));
    c->seg[S_CS] = ret_cs;
    c->ip = ret_ip;
    c->flags = (uint16_t)(cpu_flags_fixed(c) | F_IF);
    m->nproc--;
    dos_mem_free_process(m, psp);
    cpu_irq_state_changed(c);
    return 1;
}

