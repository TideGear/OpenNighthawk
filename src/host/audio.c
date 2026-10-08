/* audio.c - the AdLib and the PC speaker, rendered on the machine's clock. */
#include "audio.h"
#include "opl3.h"
#include "dbopl_bridge.h"
#include "audio_mix.h"
#include "mt32.h"
#include "speaker.h"

#include <stdlib.h>
#include <string.h>

#define BUF_FRAMES (AUDIO_RATE * 2)       /* two seconds of slack */

struct audio {
    opl3_chip opl;
    dbopl_t *dbopl;
    mt32_t *mt32;
    int32_t opl_last[2];
    uint64_t ips;
    uint64_t done;                        /* samples rendered so far */
    int16_t  buf[BUF_FRAMES * 2];
    size_t   head, count;                 /* ring of stereo frames */
    speaker_t spk;                        /* PIT counter 2 and port 61h, run here */
    uint8_t  spk_port61;                  /* the bits the speaker model last saw */
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
    speaker_init(&a->spk, ips, AUDIO_RATE, SPEAKER_REALSOUND);
    if (backend == AUDIO_OPL_DBOPL) {
        a->dbopl = dbopl_create(AUDIO_RATE);
        if (!a->dbopl) { free(a); return NULL; }
    } else OPL3_Reset(&a->opl, AUDIO_RATE);
    return a;
}

void audio_destroy(audio_t *a)
{
    if (a) { dbopl_destroy(a->dbopl); mt32_destroy(a->mt32); }
    free(a);
}

/* The speaker's full swing: GOG DOSBox 0.74 (+-5000) and DOSBox-X (0..10000)
 * both give the cone a span of 10,000. */
#define SPEAKER_SPAN 10000.0f

static void render_frame(audio_t *a, int16_t roland_left, int16_t roland_right)
{
    int32_t s[2];
    if (a->dbopl) {
        dbopl_generate(a->dbopl, s, 1);
        s[1] = s[0];
    } else {
        int16_t pair[2];
        OPL3_GenerateResampled(&a->opl, pair);
        s[0] = pair[0]; s[1] = pair[1];
    }
    /* The speaker, through a gentle DC blocker (about 3.5 Hz) so a held
     * level decays to silence as the cone does. */
    const float v = SPEAKER_SPAN * speaker_take(&a->spk, a->done);
    a->spk_dc += (v - a->spk_dc) * 0.0005f;
    const float sp = v - a->spk_dc;
    /* GOG DOSBox's AdLib mixer channel uses SetScale(2.0). Apply its
     * gain before mixing the separately driven speaker and clipping. */
    int l = 2 * opl_mixer_sample(s[0], &a->opl_last[0]) + (int)sp;
    int r = 2 * opl_mixer_sample(s[1], &a->opl_last[1]) + (int)sp;
    l += roland_left; r += roland_right;
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

/* Render up to `target` samples; the speaker model runs alongside, sample by
 * sample, so it never gets more than one sample ahead of the frames taken. */
static void render_to(audio_t *a, uint64_t target, uint64_t icount)
{
    while (a->done < target) {
        unsigned frames = (unsigned)(target - a->done > 256 ? 256 : target - a->done);
        int16_t roland[512] = { 0 };
        if (a->mt32) mt32_render(a->mt32, roland, frames);
        for (unsigned frame = 0; frame < frames; ++frame) {
            /* the first clock count at or past the end of the sample under way */
            uint64_t end = muldiv(a->done + 1, a->ips, AUDIO_RATE);
            if (muldiv(end, AUDIO_RATE, a->ips) < a->done + 1) end++;
            speaker_advance(&a->spk, end < icount ? end : icount);
            render_frame(a, roland[frame * 2], roland[frame * 2 + 1]);
        }
    }
    speaker_advance(&a->spk, icount);
}

void audio_advance(audio_t *a, uint64_t icount)
{
    render_to(a, muldiv(icount, AUDIO_RATE, a->ips), icount);
}

void audio_opl_write(audio_t *a, uint64_t icount, uint8_t reg, uint8_t val)
{
    audio_advance(a, icount);
    if (a->dbopl) dbopl_write(a->dbopl, reg, val);
    else OPL3_WriteReg(&a->opl, reg, val);
}

int audio_enable_mt32(audio_t *a, const char *control, const char *pcm,
                       char *error, size_t error_size)
{
    if (!a || a->done || a->mt32) {
        if (error && error_size) {
            const char *reason = "Munt must be enabled once before audio rendering starts";
            size_t n = strlen(reason);
            if (n >= error_size) n = error_size - 1;
            memcpy(error, reason, n); error[n] = 0;
        }
        return 0;
    }
    a->mt32 = mt32_create(control, pcm, AUDIO_RATE, error, error_size);
    return a->mt32 != NULL;
}

int audio_midi_byte(audio_t *a, uint64_t icount, uint8_t byte)
{
    if (!a || !a->mt32) return 1;
    audio_advance(a, icount);
    return mt32_byte(a->mt32, byte);
}

int audio_set_speaker_model(audio_t *a, int model)
{
    if (!a || a->done || a->spk.events || (model != SPEAKER_REALSOUND && model != SPEAKER_PWM)) return 0;
    speaker_init(&a->spk, a->ips, AUDIO_RATE, (speaker_model)model);
    return 1;
}

void audio_speaker(audio_t *a, const machine_t *m, uint64_t icount)
{
    audio_speaker_event(a, icount, m->port61, m->pit[2].reload,
                        m->pit[2].mode, m->pit[2].null_count);
}

/* The machine reports every change to counter 2 and port 61h bits 0-1 after
 * making it; which one it was follows from its state: bits that differ from
 * the last seen, else a control word (the counter waits for a count), else a
 * count. */
void audio_speaker_event(audio_t *a, uint64_t icount, uint8_t port61,
                         uint16_t reload, uint8_t mode, uint8_t null_count)
{
    audio_advance(a, icount);
    port61 &= 3;
    if (port61 != a->spk_port61) {
        a->spk_port61 = port61;
        speaker_port61(&a->spk, icount, port61);
    } else if (null_count) speaker_control(&a->spk, icount, mode);
    else speaker_count(&a->spk, icount, reload);
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
