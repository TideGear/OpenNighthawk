/* Render timestamped OPL2 writes with GOG DOSBox's synthesis core. */
#include "dbopl_bridge.h"
#include "audio_mix.h"
#include <stdio.h>
#include <stdlib.h>
#define RATE 44100
static void le32(FILE *f, uint32_t v)
{
    for (int i = 0; i < 4; ++i) fputc((v >> (i * 8)) & 255, f);
}
static uint64_t samples(uint64_t clock, uint64_t ips)
{
    return clock / ips * RATE + clock % ips * RATE / ips;
}
static int render(dbopl_t *chip, FILE *out, uint64_t *at, uint64_t until, int32_t *last)
{
    while (*at < until) {
        int32_t mono[512]; int16_t stereo[1024];
        size_t n = until - *at > 512 ? 512 : (size_t)(until - *at);
        dbopl_generate(chip, mono, n);
        for (size_t i = 0; i < n; ++i) {
            int64_t v = (int64_t)opl_mixer_sample(mono[i], last) * 2;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            stereo[i * 2] = stereo[i * 2 + 1] = (int16_t)v;
        }
        if (fwrite(stereo, 4, n, out) != n) return 0;
        *at += n;
    }
    return 1;
}
int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: dbopl_render OPL.LOG OUT.WAV IPS END_CLOCK\n"); return 2;
    }
    uint64_t ips = strtoull(argv[3], NULL, 10), end = strtoull(argv[4], NULL, 10);
    if (!ips || !end || samples(end, ips) > (0xffffffffu - 36u) / 4u) return 2;
    FILE *in = fopen(argv[1], "r"), *out = fopen(argv[2], "wb");
    dbopl_t *chip = dbopl_create(RATE);
    if (!in || !out || !chip) return 1;
    uint32_t bytes = (uint32_t)samples(end, ips) * 4u;
    fwrite("RIFF", 1, 4, out); le32(out, 36 + bytes); fwrite("WAVEfmt ", 1, 8, out);
    le32(out, 16); fwrite("\1\0\2\0", 1, 4, out); le32(out, RATE);
    le32(out, RATE * 4); fwrite("\4\0\20\0data", 1, 8, out); le32(out, bytes);
    uint64_t at = 0, last = 0; unsigned long long clock; unsigned reg, value;
    int parsed, rc = 0; int32_t last_sample = 0;
    while ((parsed = fscanf(in, "%llu %x %x", &clock, &reg, &value)) == 3) {
        if (clock < last || reg > 255 || value > 255) { rc = 1; break; }
        if (clock > end) break;
        if (!render(chip, out, &at, samples(clock, ips), &last_sample)) { rc = 1; break; }
        dbopl_write(chip, (uint8_t)reg, (uint8_t)value); last = clock;
    }
    if (parsed != 3 && parsed != EOF) rc = 1;
    if (!rc && !render(chip, out, &at, samples(end, ips), &last_sample)) rc = 1;
    if (fclose(out)) rc = 1;
    fclose(in); dbopl_destroy(chip);
    printf("%llu frames at %d Hz%s\n", (unsigned long long)at, RATE, rc ? "; FAILED" : "");
    return rc;
}
