/* subtitles.h - subtitles for the game's radio calls (docs/roadmap.md, Phase 3 "Accessibility").
 *
 * Under the Roland and PC-speaker drivers the nine spoken words (SPEECH.117) reach the speaker as pulse-width
 * modulation: PIT counter 2 in mode 0, one count per sample, each sample written twice, at 15.1 kHz. The counts
 * follow the file's bytes (a rising straight line, correlation 0.99 over a whole word), so the detector finds which
 * word is playing by correlating the most recent counts with a stretch of each word taken from the file. A
 * stretch from the middle of the word is used: the first 1,200 samples of a burst run slightly out of step with
 * the file (up to 15 samples early), and a stretch from there would not match.
 *
 * The detector reads only what the host already sees of the speaker. It never touches the machine, and it
 * needs the player's own SPEECH.117. Under the AdLib driver the speech is OPL writes and is not detected. */
#ifndef F117R_SUBTITLES_H
#define F117R_SUBTITLES_H

#include <stddef.h>
#include <stdint.h>

#define SUB_WORDS 9
#define SUB_WINDOW 1000                 /* samples correlated at once (about 0.13 s) */
#define SUB_RING 2048

typedef struct {
    float tmpl[SUB_WORDS][SUB_WINDOW];  /* each word's stretch, minus its mean */
    float tnorm[SUB_WORDS];
    int toff[SUB_WORDS];                /* where the stretch begins in its word */
    /* two rings: the count stream's even and odd events (a sample is written twice, and which write starts a
     * pair is not known) */
    float ring[2][SUB_RING];
    unsigned long n[2];
    double sum[2], sumsq[2];
    int locked;                         /* the phase that found a word in this burst, or -1 */
    unsigned long events;
    unsigned long last_ms;              /* when the last count arrived */
    /* what was heard */
    int last_word;
    unsigned long last_word_n;
    unsigned long word_ms;              /* when the last word was found */
    char text[96];
    int ready;
} subtitles;

/* Build the templates from the bytes of SPEECH.117 (the 26,408-byte file). 0 if the file is not that file. */
int sub_init(subtitles *s, const uint8_t *speech, size_t size);

/* One count written to PIT counter 2 (mode 0, speaker on), at host time now_ms. */
void sub_count(subtitles *s, int count, unsigned long now_ms);

/* The subtitle to show at now_ms, or NULL. */
const char *sub_text(const subtitles *s, unsigned long now_ms);

/* The word names, for tests and display. */
const char *sub_word_name(int word);

#endif
