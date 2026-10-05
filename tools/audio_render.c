/* Render an OPL or MIDI event log using the application's actual audio path. */
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void le32(FILE *f, unsigned long v)
{
    for (int i = 0; i < 4; ++i) fputc((int)((v >> (8 * i)) & 255), f);
}

static int drain(audio_t *audio, FILE *out, uint64_t *frames)
{
    int16_t samples[2048];
    size_t n;
    while ((n = audio_take(audio, samples, 1024))) {
        if (fwrite(samples, 4, n, out) != n) return 0;
        *frames += n;
    }
    return 1;
}

static int advance(audio_t *audio, FILE *out, uint64_t *at, uint64_t until,
                   uint64_t ips, uint64_t *frames)
{
    while (*at < until) {
        uint64_t next = *at + ips / 100;
        if (next > until) next = until;
        audio_advance(audio, next);
        if (!drain(audio, out, frames)) return 0;
        *at = next;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6 && argc != 8) {
        fprintf(stderr, "usage: audio_render OPL.LOG OUT.WAV IPS END_CLOCK [dbopl|nuked]\n"
                        "       audio_render MIDI.LOG OUT.WAV IPS END_CLOCK --mt32 CONTROL.ROM PCM.ROM\n");
        return 2;
    }
    uint64_t ips = strtoull(argv[3], NULL, 10), end = strtoull(argv[4], NULL, 10);
    if (ips < 100 || !end) return 2;
    FILE *input = fopen(argv[1], "r"), *out = fopen(argv[2], "wb");
    if (!input || !out) { perror("audio files"); if (input) fclose(input); if (out) fclose(out); return 1; }
    audio_opl_backend backend = AUDIO_OPL_DBOPL;
    if (argc == 6) {
        if (!strcmp(argv[5], "nuked")) backend = AUDIO_OPL_NUKED;
        else if (strcmp(argv[5], "dbopl")) { fclose(input); fclose(out); return 2; }
    }
    audio_t *audio = audio_create_backend(ips, backend);
    if (!audio) { fclose(input); fclose(out); return 1; }
    int midi = argc == 8;
    if (midi) {
        char error[256];
        if (strcmp(argv[5], "--mt32")) { audio_destroy(audio); fclose(input); fclose(out); return 2; }
        if (!audio_enable_mt32(audio, argv[6], argv[7], error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            audio_destroy(audio); fclose(input); fclose(out); return 1;
        }
    }
    fwrite("RIFF", 1, 4, out); le32(out, 0); fwrite("WAVEfmt ", 1, 8, out);
    le32(out, 16); fwrite("\1\0\2\0", 1, 4, out); le32(out, AUDIO_RATE);
    le32(out, AUDIO_RATE * 4); fwrite("\4\0\20\0data", 1, 8, out); le32(out, 0);
    uint64_t at = 0, frames = 0;
    unsigned long long clock;
    unsigned reg, value;
    int rc = 0, parsed;
    while ((parsed = midi ? fscanf(input, "%llu %x", &clock, &value)
                          : fscanf(input, "%llu %x %x", &clock, &reg, &value)) == (midi ? 2 : 3)) {
        if (clock < at || (!midi && reg > 255) || value > 255) { rc = 1; break; }
        if (clock > end) break;
        if (!advance(audio, out, &at, clock, ips, &frames)) { rc = 1; break; }
        if (midi) { if (!audio_midi_byte(audio, clock, (uint8_t)value)) { rc = 1; break; } }
        else audio_opl_write(audio, clock, (uint8_t)reg, (uint8_t)value);
    }
    if (parsed != (midi ? 2 : 3) && parsed != EOF) rc = 1;
    if (!rc && !advance(audio, out, &at, end, ips, &frames)) rc = 1;
    if (frames > (0xFFFFFFFFu - 36u) / 4u) rc = 1;
    if (fseek(out, 4, SEEK_SET)) rc = 1;
    le32(out, (unsigned long)(36 + frames * 4));
    if (fseek(out, 40, SEEK_SET)) rc = 1;
    le32(out, (unsigned long)(frames * 4));
    if (fclose(out)) rc = 1;
    fclose(input); audio_destroy(audio);
    printf("%llu frames at %d Hz%s\n", (unsigned long long)frames, AUDIO_RATE, rc ? "; FAILED" : "");
    return rc;
}
