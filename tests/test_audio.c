/* Actual host audio: timestamped writes must ignore caller chunk boundaries. */
#include "audio.h"
#include "audio_mix.h"
#include "speaker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static void tone(audio_t *a, uint64_t at)
{
    static const uint8_t pairs[][2] = {
        {0x20,0x21},{0x23,0x21},{0x40,0x10},{0x43,0},
        {0x60,0xf0},{0x63,0xf0},{0x80,0x0f},{0x83,0x0f},
        {0xc0,0},{0xa0,0x98},{0xb0,0x31}
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; ++i)
        audio_opl_write(a, at, pairs[i][0], pairs[i][1]);
}
int main(void)
{
    int32_t last = 0;
    CHECK(opl_mixer_sample(186, &last) == 185);
    CHECK(opl_mixer_sample(1477, &last) == 1476);
    CHECK(opl_mixer_sample(100, &last) == 100);
    last = 0; CHECK(opl_mixer_sample(32768, &last) == 32766);
    CHECK(opl_mixer_sample(0, &last) == 2);
    CHECK(!audio_create(0));
    CHECK(!audio_create_backend(AUDIO_RATE, (audio_opl_backend)99));
    static int16_t x[16384], y[16384];
    for (int backend = AUDIO_OPL_DBOPL; backend <= AUDIO_OPL_NUKED; ++backend) {
        audio_t *a = audio_create_backend(AUDIO_RATE, (audio_opl_backend)backend);
        audio_t *b = audio_create_backend(AUDIO_RATE, (audio_opl_backend)backend);
        CHECK(a && b);
        tone(a, 100); tone(b, 100);
        audio_opl_write(a, 4000, 0xb0, 0x11);
        for (uint64_t at = 101; at < 4000; at += 71) audio_advance(b, at);
        audio_opl_write(b, 4000, 0xb0, 0x11);
        audio_advance(a, 8192);
        for (uint64_t at = 4001; at < 8192; at += 71) audio_advance(b, at);
        audio_advance(b, 8192);
        CHECK(audio_take(a, x, 8192) == 8192);
        CHECK(audio_take(b, y, 8192) == 8192);
        CHECK(!memcmp(x, y, sizeof x));
        for (int i = 0; i < 200; ++i) CHECK(x[i] == 0);
        int audible = 0;
        for (int i = 200; i < 8000; ++i) audible |= x[i] != 0;
        CHECK(audible);
        CHECK(audio_take(a, x, 1) == 0);
        audio_destroy(a); audio_destroy(b);
    }
    audio_t *a = audio_create(AUDIO_RATE); CHECK(a);
    char error[256];
    CHECK(!audio_enable_mt32(a, "missing-control.rom", "missing-pcm.rom", error, sizeof error));
    CHECK(error[0]);
    audio_midi_byte(a, 1000, 0x90);  /* Disabled backend leaves the audio clock alone. */
    /* The speaker, through the machine's hook: counter 2 in mode 3 at
     * 1193182/1193 Hz with the gate and data bit on plays a tone. */
    machine_t *m = (machine_t *)calloc(1, sizeof *m); CHECK(m);
    m->port61 = 3; audio_speaker(a, m, 0);                      /* gate and data on */
    m->pit[2].mode = 3; m->pit[2].null_count = 1; audio_speaker(a, m, 0);   /* control word */
    m->pit[2].reload = 1193; m->pit[2].null_count = 0; audio_speaker(a, m, 0);
    audio_advance(a, 4096);
    CHECK(audio_take(a, x, 4096) == 4096);
    CHECK(!audio_enable_mt32(a, "missing-control.rom", "missing-pcm.rom", error, sizeof error));
    CHECK(strstr(error, "before audio rendering"));
    int positive = 0, negative = 0;
    for (int i = 0; i < 8192; ++i) { positive |= x[i] > 0; negative |= x[i] < 0; }
    CHECK(positive && negative);
    /* Off: the cone holds still (the DC blocker lets the last level decay). */
    m->port61 = 0; audio_speaker(a, m, 4096); audio_advance(a, 8192);
    CHECK(audio_take(a, x, 4096) == 4096);
    for (int i = 4; i < 8192; i += 2) CHECK(x[i] >= x[i - 2] - 1 && x[i] <= 0);
    audio_destroy(a); free(m);

    /* Speaker tests on the PIT's own clock (one clock an instruction). */
    const uint64_t pit = 1193182;
    /* Mode 3: a count rewritten while counting (the speaker driver's
     * vibrato and noise, every 3.3 ms) waits for the half-cycle's end, so
     * the tone goes on; it does not restart the wave (which held the cone
     * high for as long as the writes came faster than half a period). */
    {
        audio_t *t = audio_create(pit); CHECK(t);
        audio_speaker_event(t, 0, 0, 0, 3, 1);                 /* control word: mode 3 */
        audio_speaker_event(t, 0, 3, 0, 3, 1);                 /* gate and data on */
        audio_speaker_event(t, 0, 3, 1193, 3, 0);              /* 1000 Hz */
        for (uint64_t at = 300; at < 8 * 4096; at += 300) audio_speaker_event(t, at, 3, 1193, 3, 0);
        audio_advance(t, 8 * 4096);
        size_t n = audio_take(t, x, 8192);
        CHECK(n > 1000);
        int crossings = 0;
        for (size_t i = 200; i < n; ++i) crossings += (x[2 * i] > 0) != (x[2 * i - 2] > 0);
        /* 1000 Hz over (n - 200) samples at 44.1 kHz, two crossings a period */
        const double want = 2.0 * 1000.0 * (double)(n - 200) / AUDIO_RATE;
        CHECK(crossings > want * 0.9 && crossings < want * 1.1);
        audio_destroy(t);
    }
    /* Mode 0 with the gate and data bit on is digitised sound: a count N
     * written every 79 clocks drives OUT low for N + 1 clocks of each 79.
     * realsound averages each carrier period: the cone's level is the high
     * share, (78 - N) / 79, steady within a held count; pwm keeps the
     * carrier. (The speaker model itself, before the mix's DC blocker.) */
    {
        double mean[2][2] = { { 0 } }, var[2][2] = { { 0 } };
        for (int model = 0; model < 2; ++model) {
            speaker_t *s = (speaker_t *)calloc(1, sizeof *s); CHECK(s);
            speaker_init(s, pit, AUDIO_RATE, (speaker_model)model);
            speaker_port61(s, 0, 3);
            speaker_control(s, 0, 0);
            static float lv[8000];
            uint64_t at = 1, next = 0;
            for (int i = 0; i < 2400; ++i, at += 79) {
                speaker_count(s, at, i < 1200 ? 20 : 60);
                const uint64_t done = at * AUDIO_RATE / pit;      /* samples complete before `at` */
                for (; next < done && next < 8000; ++next) lv[next] = speaker_take(s, next);
            }
            for (int p = 0; p < 2; ++p) {
                const int from = p ? 4500 : 1000;
                double sum = 0, sum2 = 0;
                for (int i = from; i < from + 2000; ++i) { sum += lv[i]; sum2 += (double)lv[i] * lv[i]; }
                mean[model][p] = sum / 2000; var[model][p] = sum2 / 2000 - mean[model][p] * mean[model][p];
            }
            free(s);
        }
        CHECK(fabs(mean[0][0] - 58.0 / 79) < 0.005 && fabs(mean[0][1] - 18.0 / 79) < 0.005);
        CHECK(var[0][0] < 1e-4 && var[0][1] < 1e-4);           /* the carrier is gone */
        CHECK(var[1][0] > 0.02);                               /* pwm keeps it */
        CHECK(fabs(mean[1][0] - mean[0][0]) < 0.01 && fabs(mean[1][1] - mean[0][1]) < 0.01);
    }
    /* Chunking: the speaker ignores how the caller slices time. */
    {
        audio_t *t = audio_create(pit), *u = audio_create(pit); CHECK(t && u);
        uint64_t at = 0;
        audio_speaker_event(t, 0, 0, 0, 3, 1); audio_speaker_event(u, 0, 0, 0, 3, 1);
        audio_speaker_event(t, 0, 3, 0, 3, 1); audio_speaker_event(u, 0, 3, 0, 3, 1);
        for (int i = 0; i < 400; ++i) {
            const uint16_t c = (uint16_t)(300 + (i * 37) % 900);
            at += 97 + (i * 13) % 71;
            audio_speaker_event(t, at, 3, c, 3, 0);
            for (uint64_t k = at - 90; k < at; k += 29) audio_advance(u, k);
            audio_speaker_event(u, at, 3, c, 3, 0);
            if (i == 200) { audio_speaker_event(t, at + 5, 1, c, 3, 0); audio_speaker_event(u, at + 5, 1, c, 3, 0); }
            if (i == 260) { audio_speaker_event(t, at + 5, 3, c, 3, 0); audio_speaker_event(u, at + 5, 3, c, 3, 0); }
        }
        audio_advance(t, at + 1000); audio_advance(u, at + 1000);
        const size_t nt = audio_take(t, x, 8192), nu = audio_take(u, y, 8192);
        CHECK(nt == nu && nt > 1000);
        CHECK(!memcmp(x, y, nt * 4));
        audio_destroy(t); audio_destroy(u);
    }
    puts("DBOPL/Nuked timestamped audio and the 8254 speaker pass");
    return 0;
}
