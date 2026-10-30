/* test_subtitles.c - the subtitle detector. Needs the player's own SPEECH.117 (its bytes are the templates and are not
 * in the repository): set F117_SPEECH_117 to it, or the test says it is skipped. F117_SPEECH_STREAM may name a text file
 * of counts, one a line, as the game wrote them to the speaker in a real flight. */
#include "subtitles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static const int CUT[SUB_WORDS + 1] = { 0, 4160, 7040, 8800, 10720, 12720, 15600, 18400, 22080, 26408 };

/* the counts for a run of words, as the game writes them: each sample twice, silence (128) between */
static int synth(const uint8_t *file, const int *seq, int n, int *out, int cap, int stray)
{
    int at = 0;
    if (stray) out[at++] = 0;                                  /* the one odd event at the start of a burst */
    for (int g = 0; g < 120; g++) out[at++] = 128;
    for (int w = 0; w < n; w++) {
        for (int i = CUT[seq[w]]; i < CUT[seq[w] + 1]; i++) {
            int c = (int)(0.2821 * file[i] + 0.27 + 0.5);
            if (c > 128) c = 128;
            if (at + 2 > cap) return -1;
            out[at++] = c;
            out[at++] = c;
        }
        for (int g = 0; g < 120; g++) out[at++] = 128;
    }
    return at;
}

/* feed counts at the speaker's pace (about 15 a millisecond) */
static void feed(subtitles *s, const int *c, int n, unsigned long *ms)
{
    for (int i = 0; i < n; i++) {
        if (i % 15 == 0) (*ms)++;
        sub_count(s, c[i], *ms);
    }
}

int main(void)
{
    const char *path = getenv("F117_SPEECH_117");
    if (!path) { puts("subtitles: skipped (set F117_SPEECH_117 to the player's SPEECH.117)"); return 0; }
    static uint8_t file[26408];
    FILE *f = fopen(path, "rb");
    CHECK(f && fread(file, 1, sizeof file, f) == sizeof file);
    fclose(f);
    static subtitles s;
    CHECK(!sub_init(&s, file, 100));                           /* not the file */
    static int buf[200000];
    unsigned long ms = 1000;
    const char *want[SUB_WORDS] = { "Primary", "Target", "Hit", "Good", "Nice", "Landing", "Cleared", "For take off", "To land" };
    for (int stray = 0; stray < 2; stray++)
        for (int w = 0; w < SUB_WORDS; w++) {
            CHECK(sub_init(&s, file, sizeof file));
            const int seq[1] = { w };
            const int n = synth(file, seq, 1, buf, 200000, stray);
            CHECK(n > 0);
            ms += 5000;
            feed(&s, buf, n, &ms);
            const char *t = sub_text(&s, ms);
            if (!t || strcmp(t, want[w])) { printf("word %d (stray %d): got '%s', want '%s'\n", w, stray, t ? t : "(none)", want[w]); return 1; }
        }
    /* chained, as the game plays them */
    static const struct { int seq[9]; int n; const char *text; } chain[] = {
        { { 6, 7 }, 2, "Cleared for take off" },
        { { 0, 1, 2 }, 3, "Primary target hit" },
        { { 3, 2 }, 2, "Good hit" },
        { { 4, 5 }, 2, "Nice landing" },
        { { 6, 8 }, 2, "Cleared to land" },
        { { 0, 1, 2, 3, 4, 5, 6, 7, 8 }, 9, "Primary target hit good nice landing cleared for take off to land" },
    };
    for (unsigned k = 0; k < sizeof chain / sizeof chain[0]; k++) {
        CHECK(sub_init(&s, file, sizeof file));
        const int n = synth(file, chain[k].seq, chain[k].n, buf, 200000, 1);
        CHECK(n > 0);
        ms += 5000;
        feed(&s, buf, n, &ms);
        const char *t = sub_text(&s, ms);
        if (!t || strcmp(t, chain[k].text)) { printf("chain %u: got '%s', want '%s'\n", k, t ? t : "(none)", chain[k].text); return 1; }
    }
    /* the line goes away */
    CHECK(sub_text(&s, ms + 3000) == NULL);
    /* noise that is not speech finds nothing */
    CHECK(sub_init(&s, file, sizeof file));
    srand(7);
    for (int i = 0; i < 40000; i++) buf[i] = 28 + rand() % 14;
    ms += 5000;
    feed(&s, buf, 40000, &ms);
    CHECK(sub_text(&s, ms) == NULL);
    /* a real flight's stream, when given */
    const char *real = getenv("F117_SPEECH_STREAM");
    if (real) {
        FILE *r = fopen(real, "r");
        CHECK(r);
        int n = 0, v;
        while (n < 200000 && fscanf(r, "%d", &v) == 1) buf[n++] = v;
        fclose(r);
        CHECK(n > 10000 && sub_init(&s, file, sizeof file));
        ms += 5000;
        feed(&s, buf, n, &ms);
        const char *t = sub_text(&s, ms);
        if (!t || strcmp(t, "Cleared for take off")) { printf("real stream: got '%s'\n", t ? t : "(none)"); return 1; }
        puts("subtitles: the real flight stream reads 'Cleared for take off'");
    }
    puts("subtitles pass");
    return 0;
}
