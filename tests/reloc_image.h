/* Test-only image loading. The production module descriptor stays unchanged. */
#ifndef F117R_TEST_RELOC_IMAGE_H
#define F117R_TEST_RELOC_IMAGE_H

#include <string.h>
#include "recomp_gen.h"

extern const char *const RC_RELOC_NAMES[];
extern const uint64_t RC_RELOC_HASHES[];
extern const uint32_t RC_RELOC_STARTS[], RC_RELOC_OFFSETS[];
extern const unsigned RC_NRELOC_MODULES, RC_NRELOCS;

typedef struct {
    const char *const *names;
    const uint64_t *hashes;
    const uint32_t *starts, *offsets;
    unsigned modules, count;
} test_reloc_table;

static int test_load_image(uint8_t *memory, uint32_t capacity, const rc_module *m,
                           uint16_t base, const test_reloc_table *t)
{
    const uint64_t at = (uint64_t)base * 16u + m->origin;
    if (at > capacity || m->size > capacity - at || !t->modules ||
        !t->names || !t->hashes || !t->starts || !t->offsets ||
        t->starts[0] != 0 || t->starts[t->modules] != t->count) return 0;
    unsigned found = t->modules;
    for (unsigned k = 0; k < t->modules; k++) {
        if (!t->names[k] || t->starts[k] > t->starts[k + 1] ||
            t->starts[k + 1] > t->count) return 0;
        if (t->hashes[k] == m->file_hash && !strcmp(t->names[k], m->name)) {
            if (found != t->modules) return 0;
            found = k;
        }
    }
    if (found == t->modules) return 0;
    const uint32_t first = t->starts[found], end = t->starts[found + 1];
    for (uint32_t k = first; k < end; k++) {
        const uint32_t off = t->offsets[k];
        if (off >= m->size || m->size - off < 2 ||
            (k > first && off <= (uint64_t)t->offsets[k - 1] + 1)) return 0;
    }
    /* Validate everything before changing memory. Fixups address the image,
     * including for a COM whose image starts at PSP:0100. */
    memcpy(memory + at, m->image, m->size);
    for (uint32_t k = first; k < end; k++) {
        uint8_t *p = memory + at + t->offsets[k];
        const uint16_t word = (uint16_t)((uint16_t)(p[0] | p[1] << 8) + base);
        p[0] = (uint8_t)word; p[1] = (uint8_t)(word >> 8);
    }
    return 1;
}

static int test_load_generated_image(uint8_t *memory, uint32_t capacity,
                                     const rc_module *m, uint16_t base)
{
    const test_reloc_table t = { RC_RELOC_NAMES, RC_RELOC_HASHES, RC_RELOC_STARTS,
        RC_RELOC_OFFSETS, RC_NRELOC_MODULES, RC_NRELOCS };
    return test_load_image(memory, capacity, m, base, &t);
}
#endif
