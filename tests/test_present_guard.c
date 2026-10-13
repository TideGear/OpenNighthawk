/* Flat-cell repair must preserve every byte and counter at every app scale. */
#include "hires.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t coarse[64000], strokes[8000];

int main(void)
{
    for (int n = 1; n <= 16; n++) {
        const size_t width = 320u * n, bytes = 64000u * n * n;
        uint8_t *fine = malloc(bytes), *expected = malloc(bytes);
        if (!fine || !expected) return 2;
        memset(strokes, 0, sizeof strokes);
        for (int y = 0; y < 200; y++) for (int x = 0; x < 320; x++) {
            const unsigned a = (unsigned)(320 * y + x);
            const uint8_t colour = (uint8_t)((x / 8 + 17 * (y / 8)) * 37);
            coarse[a] = colour;
            if (a % 19 == 0) strokes[a / 8] |= (uint8_t)(1u << (a % 8));
            for (int j = 0; j < n; j++)
                memset(fine + (size_t)(y * n + j) * width + x * n, colour, (size_t)n);
            /* Exercise every byte position in the 8/4/tail groups and
             * every fine row, including protected strokes and coarse edges. */
            if (a % 3 == 0) {
                const unsigned pos = (a / 3) % (unsigned)(n * n);
                fine[(size_t)(y * n + pos / n) * width + x * n + pos % n] ^= 0x81;
            }
        }
        memcpy(expected, fine, bytes);
        uint64_t flat = 7, agree = 11, want_flat = 7, want_agree = 11;
        int want_restored = 0;
        /* Deliberately scalar reference: examine the whole neighbourhood
         * and cell, then replace only eligible cells that differ. */
        for (int y = 1; y < 199; y++) for (int x = 1; x < 319; x++) {
            const unsigned a = (unsigned)(320 * y + x);
            if (strokes[a / 8] & (1u << (a % 8))) continue;
            int neighbours = 0;
            for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++)
                neighbours += coarse[(y + j) * 320 + x + i] == coarse[a];
            if (neighbours != 9) continue;
            want_flat++;
            int matches = 0;
            for (int j = 0; j < n; j++) for (int i = 0; i < n; i++)
                matches += expected[(size_t)(y * n + j) * width + x * n + i] == coarse[a];
            if (matches == n * n) { want_agree++; continue; }
            want_restored++;
            for (int j = 0; j < n; j++) for (int i = 0; i < n; i++)
                expected[(size_t)(y * n + j) * width + x * n + i] = coarse[a];
        }
        const int restored = hires_guard_masked(coarse, fine, n, strokes, &flat, &agree);
        if (restored != want_restored || flat != want_flat || agree != want_agree || memcmp(fine, expected, bytes)) {
            fprintf(stderr, "flat guard differs from scalar reference at scale %d\n", n);
            return 1;
        }
        free(fine); free(expected);
    }
    puts("flat guard: identical bytes and counters at all 16 scales");
    return 0;
}
