/* test_macsound.c - the Mac recording player: WAVs load and resample, ids map to resources, sounds mix and end. */
#include "macsound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void put32(FILE *f, unsigned long v) { put16(f, (unsigned)(v & 0xFFFF)); put16(f, (unsigned)(v >> 16)); }

/* an 8-bit mono WAV of n samples at `rate`, every sample `level` (0..255) */
static void write_wav(const char *path, unsigned n, unsigned rate, int level)
{
    FILE *f = fopen(path, "wb");
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, rate); put32(f, rate); put16(f, 1); put16(f, 8);
    fwrite("data", 1, 4, f); put32(f, n);
    for (unsigned i = 0; i < n; i++) fputc(level, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[1024];
    snprintf(path, sizeof path, "%s/322.wav", dir); write_wav(path, 1000, 11025, 192);   /* music id 36 */
    snprintf(path, sizeof path, "%s/304.wav", dir); write_wav(path, 500, 22050, 64);     /* effect id 4 */
    macsound *s = macsound_load(dir, 44100);
    CHECK(s);
    CHECK(macsound_resource(36) == 322 && macsound_resource(4) == 304 && macsound_resource(21) == 0);
    CHECK(macsound_has(s, 36) && macsound_has(s, 4) && !macsound_has(s, 5) && !macsound_has(s, 37));
    CHECK(!macsound_play(s, 5));
    /* 322.wav is 1000 samples at 11025 Hz: 4000 frames at 44100; level 192 is +64*256 */
    enum { N = 6000 };
    static int16_t out[N * 2];
    CHECK(macsound_play(s, 36));
    macsound_mix(s, out, N);
    CHECK(out[0] == 64 * 256 && out[1] == 64 * 256);
    CHECK(out[3999 * 2] > 16000 && out[4100 * 2] == 0);                     /* ends where the recording does */
    /* two sounds add */
    memset(out, 0, sizeof out);
    CHECK(macsound_play(s, 36) && macsound_play(s, 4));
    macsound_mix(s, out, 100);
    CHECK(out[0] == 64 * 256 - 64 * 256);                                      /* 64*256 plus (64-128)*256 */
    /* restart, and stop */
    macsound_play(s, 36);
    macsound_stop_all(s);
    memset(out, 0, sizeof out);
    macsound_mix(s, out, 100);
    CHECK(out[0] == 0);
    macsound_free(s);
    puts("mac sound passes");
    return 0;
}
