/* subtitles.c - subtitles for the game's radio calls (see subtitles.h). */
#include "subtitles.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SPEECH_SIZE 26408
/* the nine words' first sample in SPEECH.117 (8,000 a second as the file is cut: the quiet gaps) and the end */
static const int WORD_START[SUB_WORDS + 1] = { 0, 4160, 7040, 8800, 10720, 12720, 15600, 18400, 22080, SPEECH_SIZE };
static const char *const WORD_NAME[SUB_WORDS] = {
    "primary", "target", "hit", "good", "nice", "landing", "cleared", "for take off", "to land" };

#define THRESHOLD 0.60f             /* a word is heard when its correlation is at least this ... */
#define MARGIN 0.15f                /* ... and this much above the next best */
#define SAME_WORD_SAMPLES 1500      /* a word found again inside this many samples is the same one */
#define BURST_GAP_MS 120            /* no count for this long ends the burst */
#define PHRASE_GAP_MS 1500          /* a word this long after the last starts a new line */
#define SHOW_MS 2500                /* how long the line stays after its last word */

const char *sub_word_name(int word)
{
    return word >= 0 && word < SUB_WORDS ? WORD_NAME[word] : "";
}

int sub_init(subtitles *s, const uint8_t *speech, size_t size)
{
    memset(s, 0, sizeof *s);
    if (size != SPEECH_SIZE) return 0;
    for (int k = 0; k < SUB_WORDS; k++) {
        const int len = WORD_START[k + 1] - WORD_START[k];
        s->toff[k] = (int)(0.4 * len);
        if (s->toff[k] + SUB_WINDOW > len) return 0;
        double mean = 0;
        for (int i = 0; i < SUB_WINDOW; i++) mean += speech[WORD_START[k] + s->toff[k] + i];
        mean /= SUB_WINDOW;
        double norm = 0;
        for (int i = 0; i < SUB_WINDOW; i++) {
            const float t = (float)(speech[WORD_START[k] + s->toff[k] + i] - mean);
            s->tmpl[k][i] = t;
            norm += (double)t * t;
        }
        s->tnorm[k] = (float)sqrt(norm);
        if (s->tnorm[k] < 1e-3f) return 0;
    }
    s->locked = -1;
    s->last_word = -1;
    s->ready = 1;
    return 1;
}

static void new_burst(subtitles *s)
{
    for (int r = 0; r < 2; r++) {
        s->n[r] = 0;
        s->sum[r] = s->sumsq[r] = 0;
    }
    s->locked = -1;
    s->last_word = -1;
    s->events = 0;
}

/* Add the sample to ring r and, with a full window, return the best word and its score (or -1). */
static int push(subtitles *s, int r, float x, float *score, float *second)
{
    float *ring = s->ring[r];
    const unsigned long n = s->n[r];
    if (n >= SUB_WINDOW) {                                   /* the sample leaving the window */
        const float old = ring[(n - SUB_WINDOW) & (SUB_RING - 1)];
        s->sum[r] -= old;
        s->sumsq[r] -= (double)old * old;
    }
    ring[n & (SUB_RING - 1)] = x;
    s->sum[r] += x;
    s->sumsq[r] += (double)x * x;
    s->n[r] = n + 1;
    if (n + 1 < SUB_WINDOW) return -1;
    const double var = s->sumsq[r] - s->sum[r] * s->sum[r] / SUB_WINDOW;
    if (var < 1.0) return -1;                                /* a flat stretch: silence */
    const float inv = (float)(1.0 / sqrt(var));
    const unsigned long base = n + 1 - SUB_WINDOW;
    float best = -2, next = -2;
    int bestk = -1;
    for (int k = 0; k < SUB_WORDS; k++) {
        const float *t = s->tmpl[k];
        float acc = 0;
        for (int i = 0; i < SUB_WINDOW; i++) acc += ring[(base + i) & (SUB_RING - 1)] * t[i];
        const float c = acc * inv / s->tnorm[k];
        if (c > best) { next = best; best = c; bestk = k; }
        else if (c > next) next = c;
    }
    *score = best;
    *second = next;
    return bestk;
}

static void heard(subtitles *s, int word, unsigned long now_ms)
{
    if (!s->text[0] || now_ms - s->word_ms > PHRASE_GAP_MS) s->text[0] = 0;   /* a new line */
    const size_t used = strlen(s->text);
    if (used + strlen(WORD_NAME[word]) + 2 < sizeof s->text)
        snprintf(s->text + used, sizeof s->text - used, "%s%s", used ? " " : "", WORD_NAME[word]);
    if (s->text[0] >= 'a' && s->text[0] <= 'z') s->text[0] = (char)(s->text[0] - 'a' + 'A');
    s->word_ms = now_ms;
}

void sub_count(subtitles *s, int count, unsigned long now_ms)
{
    if (!s->ready) return;
    if (now_ms - s->last_ms > BURST_GAP_MS) new_burst(s);
    s->last_ms = now_ms;
    const int r = (int)(s->events++ & 1);
    if (s->locked >= 0 && r != s->locked) {                  /* keep the other phase's window moving, no scoring */
        float *ring = s->ring[r];
        const unsigned long n = s->n[r];
        if (n >= SUB_WINDOW) {
            const float old = ring[(n - SUB_WINDOW) & (SUB_RING - 1)];
            s->sum[r] -= old;
            s->sumsq[r] -= (double)old * old;
        }
        ring[n & (SUB_RING - 1)] = (float)count;
        s->sum[r] += count;
        s->sumsq[r] += (double)count * count;
        s->n[r] = n + 1;
        return;
    }
    float score = 0, second = 0;
    const int k = push(s, r, (float)count, &score, &second);
    if (k < 0 || score < THRESHOLD || score - second < MARGIN) return;
    if (k == s->last_word && s->n[r] - s->last_word_n < SAME_WORD_SAMPLES) {
        s->last_word_n = s->n[r];                            /* still inside the same word */
        return;
    }
    s->last_word = k;
    s->last_word_n = s->n[r];
    s->locked = r;
    heard(s, k, now_ms);
}

const char *sub_text(const subtitles *s, unsigned long now_ms)
{
    if (!s->ready || !s->text[0] || now_ms - s->word_ms > SHOW_MS) return NULL;
    return s->text;
}
