/* Actual host audio: timestamped writes must ignore caller chunk boundaries. */
#include "audio.h"
#include "audio_mix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    machine_t *m = (machine_t *)calloc(1, sizeof *m); CHECK(m);
    m->port61 = 3; m->pit[2].mode = 3; m->pit[2].reload = 1193;
    audio_speaker(a, m, 0); audio_advance(a, 4096);
    CHECK(audio_take(a, x, 4096) == 4096);
    CHECK(!audio_enable_mt32(a, "missing-control.rom", "missing-pcm.rom", error, sizeof error));
    CHECK(strstr(error, "before audio rendering"));
    int positive = 0, negative = 0;
    for (int i = 0; i < 8192; ++i) { positive |= x[i] > 0; negative |= x[i] < 0; }
    CHECK(positive && negative);
    m->port61 = 0; audio_speaker(a, m, 4096); audio_advance(a, 8192);
    CHECK(audio_take(a, x, 4096) == 4096);
    for (int i = 0; i < 8192; ++i) CHECK(x[i] == 0);
    audio_destroy(a);
    /* Mode 0 with the gate and data bit on is digitised sound: the count is a
     * held level, (min(count, 80) - 40) x 125, as in GOG DOSBox - not a pulse
     * train point-sampled at the output rate (that aliased into a screech). */
    a = audio_create(AUDIO_RATE); CHECK(a);
    m->port61 = 3; m->pit[2].mode = 0; m->pit[2].reload = 60;
    audio_speaker(a, m, 0); audio_advance(a, 64);
    m->pit[2].reload = 20; audio_speaker(a, m, 64); audio_advance(a, 128);
    m->pit[2].reload = 200; audio_speaker(a, m, 128); audio_advance(a, 129);
    CHECK(audio_take(a, x, 129) == 129);
    for (int i = 0; i < 64; ++i) CHECK(x[2 * i] > 2300 && x[2 * i] <= 2500);      /* +2500, the DC blocker barely moved */
    for (int i = 64; i < 128; ++i) CHECK(x[2 * i] < -2400 && x[2 * i] > -2700);  /* -2500 */
    CHECK(x[2 * 128] > 4800);                                                     /* counts above 80 clamp to +5000 */
    /* a change part-way through a sample is averaged into it */
    audio_destroy(a);
    a = audio_create(2 * AUDIO_RATE); CHECK(a);
    m->pit[2].reload = 80; audio_speaker(a, m, 0);
    m->pit[2].reload = 0; audio_speaker(a, m, 1); audio_advance(a, 2);
    CHECK(audio_take(a, x, 1) == 1);
    CHECK(x[0] > -200 && x[0] < 200);                                            /* half +5000, half -5000 */
    audio_destroy(a); free(m);
    puts("DBOPL/Nuked timestamped audio and speaker gate pass");
    return 0;
}
