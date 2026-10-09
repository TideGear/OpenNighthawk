/* test_drawlist.c - the Stage 1 replay in C (src/present/drawlist.c) against
 * an observer log's page dumps.
 *
 *   test_drawlist LOG [--carry] [--logged] [--quiet]
 *
 * LOG is f117run --observe output taken with F117R_OBSERVE_PAGES=1. Each
 * phase is rebuilt from the page dumps at its start by replaying every record
 * in order and compared, byte for byte, with the dumps at its end; the lines
 * printed are tools/drawlist_frame.py's, so the two can be diffed. --logged
 * copies a page copy's logged source bytes as the Python replay does (by
 * default the replay's own source page is used). --carry seeds each page once,
 * from its first dump, and carries the replay from phase to phase, as a live
 * replay must. Exit 1 when a phase differs. */
#include "drawlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t len = 0;
    if (!*buf) { *cap = 1 << 16; *buf = (char *)malloc(*cap); }
    for (;;) {
        if (!fgets(*buf + len, (int)(*cap - len), f)) return len ? *buf : NULL;
        len += strlen(*buf + len);
        if (len && (*buf)[len - 1] == '\n') return *buf;
        *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int carry = 0, logged = 0, quiet = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--carry")) carry = 1;
        else if (!strcmp(argv[i], "--logged")) logged = 1;
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: test_drawlist LOG [--carry] [--logged] [--quiet]\n"); return 2; }
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot read %s\n", path); return 2; }

    static drawlist d;
    drawlist_init(&d);
    d.prefer_logged = logged;
    /* the dump at each page's phase start */
    static struct { uint16_t seg; unsigned size; uint8_t b[65536]; } start[DRAWLIST_PAGES];
    int nstart = 0;
    static int32_t v[3 + 3 * 65536 + 16];    /* the longest record: an 'x' with every byte changed */
    static uint8_t z[65536];
    char *buf = NULL;
    size_t cap = 0;
    int phase = 0;
    unsigned start_origin = 0;
    unsigned long long work_phases = 0, work_exact = 0, work_changed = 0, copied = 0;
    unsigned long long disp_phases = 0, disp_exact = 0, disp_changed = 0;
    char *line;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == 'V' || k == 'P' || k == '\n' || !k) continue;
        /* the values after the clock */
        char *p = line + 1;
        strtoull(p, &p, 10);
        int n = 0;
        for (;;) {
            char *e;
            const long x = strtol(p, &e, 10);
            if (e == p || n == (int)(sizeof v / sizeof v[0])) break;
            v[n++] = (int32_t)x;
            p = e;
        }
        if (k == 'Z' || k == 'Y') {
            const uint16_t seg = (uint16_t)v[0];
            const unsigned size = (unsigned)(n - 2);
            for (unsigned a = 0; a < size; a++) z[a] = (uint8_t)v[2 + a];
            const drawlist_page *rep = drawlist_get(&d, seg);
            int si;
            for (si = 0; si < nstart && start[si].seg != seg; si++) {}
            if (rep && si < nstart) {
                const unsigned cmp = size < rep->size ? size : rep->size;
                unsigned diffs = 0, changed = 0, first[4];
                for (unsigned a = 0; a < cmp; a++) {
                    if (rep->b[a] != z[a]) { if (diffs < 4) first[diffs] = a; diffs++; }
                    if (start[si].b[a] != z[a]) changed++;
                }
                if (!quiet) {
                    printf("phase %d %s page (origin at start %04X): %u bytes changed; replay differs at %u",
                           phase, k == 'Z' ? "work" : "display", start_origin, changed, diffs);
                    for (unsigned j = 0; j < diffs && j < 4; j++)
                        printf("%s%04X:%d/%d", j ? ", " : " (first ", first[j], rep->b[first[j]], z[first[j]]);
                    if (diffs) printf(")");
                    if (k == 'Z') printf("; %u bytes copied, not replayed", d.ncopied);
                    printf("\n");
                }
                if (k == 'Z') { work_phases++; work_exact += !diffs; work_changed += changed; copied += d.ncopied; }
                else { disp_phases++; disp_exact += !diffs; disp_changed += changed; }
            }
            if (k == 'Z') {
                start_origin = (uint16_t)v[1];
                phase++;
            }
            if (!carry || !rep) drawlist_seed(&d, k, seg, (uint16_t)v[1], z, size);
            else if (k == 'Z') drawlist_phase(&d, seg, (uint16_t)v[1]);
            if (si == nstart && nstart < DRAWLIST_PAGES) start[nstart++].seg = seg;
            if (si < DRAWLIST_PAGES) { start[si].size = size; memcpy(start[si].b, z, size); }
            continue;
        }
        drawlist_record(&d, k, v, n);
    }
    fclose(f);
    free(buf);
    if (d.blits_no_source) printf("blits without source bytes: %llu\n", d.blits_no_source);
    if (d.recolour_no_rule) printf("colour replaces with no rule: %llu\n", d.recolour_no_rule);
    if (d.outline_no_rule) printf("outline edges with no rule: %llu\n", d.outline_no_rule);
    if (d.copies_no_source) printf("page copies without source bytes: %llu\n", d.copies_no_source);
    if (d.sprites_no_source) printf("sprites without source bytes: %llu\n", d.sprites_no_source);
    printf("%llu phases rebuilt from the draw list, %llu exact; %llu bytes changed in all, %llu of them copied from the "
           "original rather than replayed\n", work_phases, work_exact, work_changed, copied);
    if (disp_phases)
        printf("display page: %llu phases rebuilt, %llu exact; %llu bytes changed in all\n", disp_phases, disp_exact, disp_changed);
    return work_exact == work_phases && disp_exact == disp_phases ? 0 : 1;
}
