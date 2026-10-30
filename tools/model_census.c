/* model_census.c - run the scene mesh decoder over every model of the shipped model banks (Remastered stage R2/R3,
 * docs/remastered-design.md). A *.3DX file begins with a table of word offsets, one per model; it is loaded at the
 * start of a 64 KB page, as the game's model bank is. For each model and each detail level (depth 0 to 7) the decoder
 * is run on the real bytes, and the census prints how many decode, how they fail, and the largest mesh.
 *
 *   model_census FILE.3DX ...
 */
#include "scene_mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static uint8_t bank[SCENE_SEGMENT];
    int bad_files = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { fprintf(stderr, "cannot read %s\n", argv[a]); bad_files++; continue; }
        /* The decoder reads a full 64 KB page: the file loaded at its start, zero-filled after. Word 0 bounds the
         * model index and the word after it, at 2 + 2 * model, points at the model's detail table. */
        memset(bank, 0, sizeof bank);
        const size_t n = fread(bank, 1, sizeof bank, f);
        fclose(f);
        const unsigned models = (bank[0] | bank[1] << 8) / 2;   /* the file starts with its offset table */
        (void)n;
        unsigned ok = 0, tried = 0, errors[8] = { 0 }, empty = 0, maxv = 0, maxp = 0, maxe = 0, maxm = 0, maxd = 0;
        for (unsigned model = 0; model < models; model++) {
            for (unsigned depth = 0; depth < 8; depth++) {
                scene_error err = SCENE_OK;
                tried++;
                scene_mesh *m = scene_mesh_decode(bank, sizeof bank, (uint16_t)model, (uint16_t)depth, &err);
                if (!m) { errors[err < 8 ? err : 7]++; continue; }
                ok++;
                if (!m->nvertices && !m->nplanes) empty++;
                if (m->nvertices > maxv) { maxv = m->nvertices; maxm = model; maxd = depth; }
                if (m->nplanes > maxp) maxp = m->nplanes;
                if (m->nedges > maxe) maxe = m->nedges;
                scene_mesh_free(m);
            }
        }
        printf("%-14s %3u models x 8 levels: %4u decode (%u empty), failures: input %u, format %u, unsupported %u, memory %u; "
               "largest %u vertices (model %u level %u), %u planes, %u edges\n",
               argv[a], models, ok, empty, errors[SCENE_INPUT], errors[SCENE_FORMAT], errors[SCENE_UNSUPPORTED],
               errors[SCENE_MEMORY], maxv, maxm, maxd, maxp, maxe);
        (void)tried;
    }
    return bad_files ? 1 : 0;
}
