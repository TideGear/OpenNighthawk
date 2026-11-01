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
    CHECK(macsound_resource(3) == 200 && macsound_resource(29) == 323 && macsound_resource(30) == 325 && macsound_resource(331) == 323);
    CHECK(macsound_resource(31) == 0 && macsound_resource(2) == 0 && macsound_resource(32) == 0);
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

    /* the radio calls: the words of each, from the driver's own lists (macsound.c), played one after another */
    {
        int w[3];
        CHECK(macsound_speech_words(41, 0, w) == 3 && w[0] == 0 && w[1] == 1 && w[2] == 2);       /* primary target hit */
        CHECK(macsound_speech_words(42, 0, w) == 2 && w[0] == 1 && w[1] == 2);                    /* target hit */
        CHECK(macsound_speech_words(43, 1, w) == 2 && w[0] == 3 && w[1] == 5);                    /* good landing (odd tick) */
        CHECK(macsound_speech_words(43, 2, w) == 2 && w[0] == 4 && w[1] == 5);                    /* nice landing (even tick) */
        CHECK(macsound_speech_words(44, 0, w) == 2 && w[0] == 6 && w[1] == 7);                    /* cleared for take off */
        CHECK(macsound_speech_words(45, 0, w) == 2 && w[0] == 6 && w[1] == 8);                    /* cleared to land */
        CHECK(macsound_speech_words(40, 0, w) == 0 && macsound_speech_words(46, 0, w) == 0 && macsound_speech_words(3, 0, w) == 0);
        /* nine words of different lengths and levels at 8 kHz resampled to 44.1 kHz: word k is 100 + 10k samples at level 128 + 8 (k + 1) */
        for (int k = 0; k < 9; k++) { snprintf(path, sizeof path, "%s/%d.wav", dir, 1000 + k); write_wav(path, 100 + 10 * (unsigned)k, 44100, 128 + 8 * (k + 1)); }
        macsound *sp = macsound_load(dir, 44100);
        CHECK(sp && macsound_has(sp, 41) && macsound_has(sp, 43) && macsound_has(sp, 45) && !macsound_has(sp, 46));
        static int16_t o[1000 * 2];
        memset(o, 0, sizeof o);
        CHECK(macsound_play_tick(sp, 43, 1));                                                     /* good (word 3, 130 samples) then landing (word 5, 150) */
        macsound_mix(sp, o, 1000);
        CHECK(o[0] == 8 * 4 * 256 && o[129 * 2] == 8 * 4 * 256);                                   /* word 3's level to its last sample ... */
        CHECK(o[130 * 2] == 8 * 6 * 256 && o[279 * 2] == 8 * 6 * 256 && o[280 * 2] == 0);          /* ... word 5's straight after, then the end */
        memset(o, 0, sizeof o);
        CHECK(macsound_play_tick(sp, 43, 2));                                                     /* nice (word 4, 140) then landing */
        macsound_mix(sp, o, 1000);
        CHECK(o[0] == 8 * 5 * 256 && o[139 * 2] == 8 * 5 * 256 && o[140 * 2] == 8 * 6 * 256 && o[289 * 2] == 8 * 6 * 256 && o[290 * 2] == 0);
        CHECK(macsound_play_tick(sp, 41, 0) && macsound_play_tick(sp, 44, 0));                      /* two calls at once do not disturb each other */
        macsound_stop_all(sp);
        memset(o, 0, sizeof o);
        macsound_mix(sp, o, 100);
        CHECK(o[0] == 0);
        macsound_free(sp);
    }
    puts("mac sound passes");
    return 0;
}
