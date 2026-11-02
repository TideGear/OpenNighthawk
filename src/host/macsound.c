/* macsound.c - the Macintosh recorded sounds (see macsound.h). */
#include "macsound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RES 600
#define VOICES 8

typedef struct { int16_t *pcm; size_t frames; } clip;    /* mono, at the mixer's rate */
typedef struct { const clip *c; size_t pos; int res; } voice;
#define SPEECH_FIRST 1000                             /* the Mac's nine words are resources 1000 to 1008 */

struct macsound {
    clip clip[MAX_RES];
    clip word[9];                                   /* the nine words of the radio calls */
    clip phrase[VOICES];                            /* a radio call as one recording, per voice */
    voice voice[VOICES];
};

static unsigned rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static unsigned long rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24; }

/* A PCM WAV into mono int16 at `rate` (linear resampling). */
static int load_wav(const char *path, int rate, clip *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = size > 44 ? (uint8_t *)malloc((size_t)size) : NULL;
    if (!b || fread(b, 1, (size_t)size, f) != (size_t)size) { free(b); fclose(f); return 0; }
    fclose(f);
    if (memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) { free(b); return 0; }
    unsigned channels = 0, bits = 0;
    unsigned long src_rate = 0;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    for (long at = 12; at + 8 <= size;) {
        const unsigned long len = rd32(b + at + 4);
        const uint8_t *body = b + at + 8;
        if (at + 8 + (long)len > size) break;
        if (!memcmp(b + at, "fmt ", 4) && len >= 16 && rd16(body) == 1) { channels = rd16(body + 2); src_rate = rd32(body + 4); bits = rd16(body + 14); }
        else if (!memcmp(b + at, "data", 4)) { data = body; data_len = len; }
        at += 8 + (long)len + (long)(len & 1);
    }
    if (!data || !channels || channels > 2 || (bits != 8 && bits != 16) || !src_rate) { free(b); return 0; }
    const size_t bytes = bits / 8, frame_bytes = bytes * channels, n = data_len / frame_bytes;
    const size_t m = (size_t)((double)n * rate / (double)src_rate);
    int16_t *pcm = m ? (int16_t *)malloc(m * sizeof *pcm) : NULL;
    if (!pcm) { free(b); return 0; }
    for (size_t i = 0; i < m; i++) {
        const double at = (double)i * (double)src_rate / rate;
        size_t i0 = (size_t)at;
        const double frac = at - (double)i0;
        size_t i1 = i0 + 1 < n ? i0 + 1 : i0;
        double v[2];
        for (int k = 0; k < 2; k++) {
            const size_t idx = k ? i1 : i0;
            double sum = 0;
            for (unsigned ch = 0; ch < channels; ch++) {
                const uint8_t *p = data + idx * frame_bytes + ch * bytes;
                sum += bits == 8 ? ((int)p[0] - 128) * 256.0 : (double)(int16_t)rd16(p);
            }
            v[k] = sum / channels;
        }
        pcm[i] = (int16_t)(v[0] + (v[1] - v[0]) * frac);
    }
    free(b);
    out->pcm = pcm;
    out->frames = m;
    return 1;
}

macsound *macsound_load(const char *dir, int rate)
{
    macsound *s = (macsound *)calloc(1, sizeof *s);
    if (!s) return NULL;
    int found = 0;
    for (int r = 1; r < MAX_RES; r++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%d.wav", dir, r);
        if (load_wav(path, rate, &s->clip[r])) found++;
    }
    for (int k = 0; k < 9; k++) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%d.wav", dir, SPEECH_FIRST + k);
        if (load_wav(path, rate, &s->word[k])) found++;
    }
    if (!found) { free(s); return NULL; }
    return s;
}

void macsound_free(macsound *s)
{
    if (!s) return;
    for (int r = 0; r < MAX_RES; r++) free(s->clip[r].pcm);
    for (int k = 0; k < 9; k++) free(s->word[k].pcm);
    for (int k = 0; k < VOICES; k++) free(s->phrase[k].pcm);
    free(s);
}

int macsound_resource(int id)
{
    switch (id) {
    case 36: return 322;
    case 37: return 321;
    case 38: return 200;
    /* Found by the Mac program's own calls (31 October 2026, docs/sound-board.md): the intro music is the Mac's title music
     * (200); the weapons page's box choice (29) and placed weapon (30) are the Mac's 323 and 325 at the same moments; the
     * flight program's bay-door key asks for 31, which main.c passes here as 331 (START's 31 is the launch animation). */
    case 3: {                                    /* F117_MAC_INTRO=200|321|322 (diagnostic): which recording the intro music is, while it is settled by ear */
        static int pick = -1;
        if (pick < 0) { const char *e = getenv("F117_MAC_INTRO"); pick = e ? atoi(e) : 0; }
        return pick == 321 || pick == 322 ? pick : 200;
    }
    case 29: return 323;
    case 30: return 325;
    case 331: return 323;
    default: break;
    }
    return id >= 4 && id <= 20 ? 300 + id : 0;
}

/* The radio calls (ISOUND.117's handlers for ids 41 to 45 and the lists they point at: sample ranges of SPEECH.117, 8,000 a second):
 *   41 [0, 9250)                       primary target hit     (words 0 1 2)
 *   42 [4216, 9250)                    target hit             (words 1 2)
 *   43 [9251, 10571) + [12829, 15653)  good landing           (words 3 5), when cs:[2] is odd
 *      [10572, 15653)                  nice landing           (words 4 5), when it is even
 *   44 [15654, 22118)                  cleared for take off   (words 6 7)
 *   45 [15654, 18342) + [22119, 26407) cleared to land        (words 6 8)
 * and the cuts fall where the Mac's nine recordings meet (4048, 6787, 9018, 10440, 12738, 15500, 18110, 21902 samples in: the DOS
 * file is the same nine words, a little longer in total). Ids 41 and 42 share "target hit"; the file has no cut between target and hit. */
int macsound_speech_words(int id, unsigned tick, int words[3])
{
    switch (id) {
    case 41: words[0] = 0; words[1] = 1; words[2] = 2; return 3;
    case 42: words[0] = 1; words[1] = 2; return 2;
    case 43: words[0] = tick & 1 ? 3 : 4; words[1] = 5; return 2;
    case 44: words[0] = 6; words[1] = 7; return 2;
    case 45: words[0] = 6; words[1] = 8; return 2;
    default: return 0;
    }
}

int macsound_has(const macsound *s, int id)
{
    if (id >= 41 && id <= 45) {                         /* a radio call: every word of it must be there (either of 43's) */
        if (!s) return 0;
        for (unsigned tick = 0; tick < 2; tick++) {
            int words[3];
            const int n = macsound_speech_words(id, tick, words);
            for (int k = 0; k < n; k++) if (!s->word[words[k]].pcm) return 0;
        }
        return 1;
    }
    const int r = macsound_resource(id);
    return s && r > 0 && r < MAX_RES && s->clip[r].pcm;
}

int macsound_play(macsound *s, int id) { return macsound_play_tick(s, id, 0); }

int macsound_play_tick(macsound *s, int id, unsigned tick)
{
    if (!macsound_has(s, id)) return 0;
    if (id >= 41 && id <= 45) {                         /* the words one after another, in a recording of their own */
        int words[3];
        const int n = macsound_speech_words(id, tick, words);
        size_t total = 0;
        for (int k = 0; k < n; k++) total += s->word[words[k]].frames;
        int slot = -1;
        for (int i = 0; i < VOICES && slot < 0; i++) if (!s->voice[i].c) slot = i;
        if (slot < 0) slot = 0;
        int16_t *pcm = (int16_t *)malloc((total ? total : 1) * sizeof *pcm);
        if (!pcm) return 0;
        size_t at = 0;
        for (int k = 0; k < n; k++) { memcpy(pcm + at, s->word[words[k]].pcm, s->word[words[k]].frames * sizeof *pcm); at += s->word[words[k]].frames; }
        free(s->phrase[slot].pcm);                      /* this voice's last call is over or being replaced */
        s->phrase[slot].pcm = pcm; s->phrase[slot].frames = total;
        s->voice[slot].c = &s->phrase[slot]; s->voice[slot].pos = 0; s->voice[slot].res = SPEECH_FIRST + id;
        return 1;
    }
    const int r = macsound_resource(id);
    voice *v = NULL;
    for (int i = 0; i < VOICES && !v; i++) if (s->voice[i].c && s->voice[i].res == r) v = &s->voice[i];   /* restart */
    for (int i = 0; i < VOICES && !v; i++) if (!s->voice[i].c) v = &s->voice[i];
    if (!v) v = &s->voice[0];
    v->c = &s->clip[r];
    v->pos = 0;
    v->res = r;
    return 1;
}

void macsound_stop_all(macsound *s)
{
    if (s) memset(s->voice, 0, sizeof s->voice);
}

void macsound_mix(macsound *s, int16_t *out, size_t frames)
{
    if (!s) return;
    for (int i = 0; i < VOICES; i++) {
        voice *v = &s->voice[i];
        if (!v->c) continue;
        size_t n = v->c->frames - v->pos;
        if (n > frames) n = frames;
        for (size_t k = 0; k < n; k++) {
            const int x = v->c->pcm[v->pos + k];
            for (int ch = 0; ch < 2; ch++) {
                int sum = out[2 * k + ch] + x;
                out[2 * k + ch] = (int16_t)(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
            }
        }
        v->pos += n;
        if (v->pos >= v->c->frames) memset(v, 0, sizeof *v);
    }
}
