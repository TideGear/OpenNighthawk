/* Tone generation across render chunks, frequency changes and key-off. */
#include "dbopl_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void tone(dbopl_t *chip)
{
    static const uint8_t pairs[][2] = {
        {0x20,0x21},{0x23,0x21},{0x40,0x10},{0x43,0},
        {0x60,0xf0},{0x63,0xf0},{0x80,0x0f},{0x83,0x0f},
        {0xc0,0},{0xa0,0x98},{0xb0,0x31}
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; ++i)
        dbopl_write(chip, pairs[i][0], pairs[i][1]);
}
int main(void)
{
    CHECK(!dbopl_create(0));
    dbopl_t *a = dbopl_create(44100), *b = dbopl_create(44100);
    CHECK(a && b);
    int32_t x[8192], y[8192];
    dbopl_generate(a, x, 8192);
    for (int i = 0; i < 8192; ++i) CHECK(x[i] == 0);
    dbopl_destroy(a); a = dbopl_create(44100); CHECK(a);
    tone(a); tone(b);
    dbopl_generate(a, x, 8192);
    for (int i = 0; i < 8192; ++i) dbopl_generate(b, y + i, 1);
    CHECK(!memcmp(x, y, sizeof x));
    int nonzero = 0;
    for (int i = 0; i < 8192; ++i) nonzero |= x[i] != 0;
    CHECK(nonzero);
    /* A frequency change and key-off take effect at the same sample even
     * when the host requests different block sizes. */
    dbopl_write(a, 0xa0, 0x40); dbopl_write(b, 0xa0, 0x40);
    dbopl_generate(a, x, 8192);
    for (int i = 0; i < 8192; i += 128) dbopl_generate(b, y + i, 128);
    CHECK(!memcmp(x, y, sizeof x));
    dbopl_write(a, 0xb0, 0x11); dbopl_write(b, 0xb0, 0x11);
    dbopl_generate(a, x, 8192); dbopl_generate(b, y, 8192);
    CHECK(!memcmp(x, y, sizeof x));
    for (int i = 4096; i < 8192; ++i) CHECK(x[i] == 0);
    dbopl_destroy(a); dbopl_destroy(b);
    puts("DBOPL silence, chunk boundaries, frequency change and release pass");
    return 0;
}
