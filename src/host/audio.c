/* audio.c - the AdLib and the PC speaker, rendered on the machine's clock. */
#include "audio.h"
#include "opl3.h"
#include "dbopl_bridge.h"
#include "audio_mix.h"

#include <stdlib.h>
#include <string.h>

#define PIT_HZ 1193182ull
#define BUF_FRAMES (AUDIO_RATE * 2)       /* two seconds of slack */

typedef struct {
    uint64_t icount;
    uint8_t  port61;
    pit_counter pit2;
} spk_state;

struct audio {
    opl3_chip opl;
    dbopl_t *dbopl;
    int32_t opl_last[2];
    uint64_t ips;
    uint64_t done;                        /* samples rendered so far */
    int16_t  buf[BUF_FRAMES * 2];
    size_t   head, count;                 /* ring of stereo frames */
    spk_state spk;                        /* the speaker since the last change */
    float    spk_dc;                      /* a DC blocker's memory */
};

static uint64_t muldiv(uint64_t a, uint64_t b, uint64_t d)
{
    return (a / d) * b + ((a % d) * b) / d;
}

audio_t *audio_create(uint64_t ips)
{
    return audio_create_backend(ips, AUDIO_OPL_DBOPL);
}

audio_t *audio_create_backend(uint64_t ips, audio_opl_backend backend)
{
    if (!ips || (backend != AUDIO_OPL_DBOPL && backend != AUDIO_OPL_NUKED)) return NULL;
    audio_t *a = (audio_t *)calloc(1, sizeof *a);
    if (!a) return NULL;
    a->ips = ips;
    if (backend == AUDIO_OPL_DBOPL) {
        a->dbopl = dbopl_create(AUDIO_RATE);
        if (!a->dbopl) { free(a); return NULL; }
    } else OPL3_Reset(&a->opl, AUDIO_RATE);
    return a;
}

void audio_destroy(audio_t *a)
{
    if (a) dbopl_destroy(a->dbopl);
    free(a);
}

/* The speaker cone: driven by counter 2's output through the AND gate of
 * port 61 bit 1, with bit 0 gating the counter itself. */
static int speaker_level(const spk_state *s, uint64_t sample)
{
    if (!(s->port61 & 2)) return 0;
    if (!(s->port61 & 1)) return 1;        /* data bit high, counter stopped: out is high */
    const pit_counter *p = &s->pit2;
    const uint32_t full = p->reload ? p->reload : 65536u;
    const uint64_t clk = muldiv(sample, PIT_HZ, AUDIO_RATE);
    const uint64_t t = clk > p->epoch_clk ? clk - p->epoch_clk : 0;
    switch (p->mode & 7) {
    case 3: case 7: return (t % full) < (full + 1) / 2;
    case 2: case 6: return (t % full) != full - 1;
    case 0: return t >= full;
    default: return 1;
    }
}

static void render_to(audio_t *a, uint64_t target)
{
    while (a->done < target) {
        int32_t s[2];
        if (a->dbopl) {
            dbopl_generate(a->dbopl, s, 1);
            s[1] = s[0];
        } else {
            int16_t pair[2];
            OPL3_GenerateResampled(&a->opl, pair);
            s[0] = pair[0]; s[1] = pair[1];
        }
        /* The speaker, through a gentle DC blocker so a held level decays
         * to silence as the real cone does. */
        float v = speaker_level(&a->spk, a->done) && (a->spk.port61 & 2) ? 6000.0f : 0.0f;
        a->spk_dc += (v - a->spk_dc) * 0.0005f;
        float sp = (a->spk.port61 & 2) ? v - a->spk_dc : 0.0f;
        /* GOG DOSBox's AdLib mixer channel uses SetScale(2.0). Apply its
         * gain before mixing the separately driven speaker and clipping. */
        int l = 2 * opl_mixer_sample(s[0], &a->opl_last[0]) + (int)sp;
        int r = 2 * opl_mixer_sample(s[1], &a->opl_last[1]) + (int)sp;
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;
        size_t at = (a->head + a->count) % BUF_FRAMES;
        a->buf[at * 2] = (int16_t)l;
        a->buf[at * 2 + 1] = (int16_t)r;
        if (a->count < BUF_FRAMES) a->count++;
        else a->head = (a->head + 1) % BUF_FRAMES;   /* overrun: drop the oldest */
        a->done++;
    }
}

void audio_advance(audio_t *a, uint64_t icount)
{
    render_to(a, muldiv(icount, AUDIO_RATE, a->ips));
}

void audio_opl_write(audio_t *a, uint64_t icount, uint8_t reg, uint8_t val)
{
    audio_advance(a, icount);
    if (a->dbopl) dbopl_write(a->dbopl, reg, val);
    else OPL3_WriteReg(&a->opl, reg, val);
}

void audio_speaker(audio_t *a, const machine_t *m, uint64_t icount)
{
    audio_advance(a, icount);
    a->spk.icount = icount;
    a->spk.port61 = m->port61;
    a->spk.pit2 = m->pit[2];
    /* The counter's epoch is in PIT clocks; the renderer works in samples
     * converted to PIT clocks, so the two agree. */
}

size_t audio_take(audio_t *a, int16_t *out, size_t max_frames)
{
    size_t n = 0;
    while (n < max_frames && a->count) {
        out[n * 2] = a->buf[a->head * 2];
        out[n * 2 + 1] = a->buf[a->head * 2 + 1];
        a->head = (a->head + 1) % BUF_FRAMES;
        a->count--;
        n++;
    }
    return n;
}
