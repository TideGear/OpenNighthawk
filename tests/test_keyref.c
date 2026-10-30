/* test_keyref.c - the on-screen key reference: every line fits its panel, pages are filled and draw in bounds. */
#include "keyref.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    int total = 0;
    for (int page = 0; page < KEYREF_PAGES; page++) {
        int n;
        const keyref_row *rows = keyref_rows(page, &n);
        CHECK(rows && n >= 10 && n <= 16);                  /* a page fits its panel: 16 rows of 18 pixels */
        for (int i = 0; i < n; i++) {
            CHECK(rows[i].keys && rows[i].keys[0]);
            CHECK((int)strlen(rows[i].keys) <= (rows[i].what ? KEYREF_KEYS_CHARS : 40));
            if (rows[i].what) CHECK((int)strlen(rows[i].what) <= KEYREF_WHAT_CHARS);
        }
        total += n;
    }
    CHECK(total > 50);
    /* the manual's keys that matter most are all present */
    const char *must[] = { "Shift F10", "F2", "F7", "space bar", "Backspace", "Shift F8", NULL };
    for (int k = 0; must[k]; k++) {
        int found = 0;
        for (int page = 0; page < KEYREF_PAGES; page++) {
            int n;
            const keyref_row *rows = keyref_rows(page, &n);
            for (int i = 0; i < n; i++)
                if (rows[i].what && !strncmp(rows[i].keys, must[k], strlen(must[k]))) found = 1;
        }
        CHECK(found);
    }
    enum { W = 640, H = 400 };
    uint32_t *px = (uint32_t *)calloc((size_t)W * H, 4);
    CHECK(px);
    for (int page = 0; page < KEYREF_PAGES; page++) {
        memset(px, 0, (size_t)W * H * 4);
        keyref_render(page, px, W, H);
        int lit = 0, outside = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (px[y * W + x]) {
                    lit++;
                    if (x < (W - 560) / 2 || x >= (W + 560) / 2 || y < (H - 372) / 2 || y >= (H + 372) / 2) outside++;
                }
        CHECK(lit > 20000 && !outside);
    }
    free(px);
    puts("key reference passes");
    return 0;
}
