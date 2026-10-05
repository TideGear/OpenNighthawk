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
                   uint64_t step_clocks, uint64_t *frames)
{
    while (*at < until) {
        uint64_t next = until - *at < step_clocks ? until : *at + step_clocks;
        audio_advance(audio, next);
        if (!drain(audio, out, frames)) return 0;
        *at = next;
    }
    return 1;
}

typedef struct {
    uint64_t clock;
    unsigned reg, value;
} audio_event;

typedef struct {
    uint64_t clock, epoch_clk;
    uint16_t reload;
    uint8_t port61, mode;
} speaker_event;

static int next_audio_event(FILE *input, int midi, audio_event *event)
{
    unsigned long long clock;
    unsigned reg = 0, value;
    int n = midi ? fscanf(input, "%llu %x", &clock, &value)
                 : fscanf(input, "%llu %x %x", &clock, &reg, &value);
    if (n == EOF) return 0;
    if (n != (midi ? 2 : 3) || (!midi && reg > 255) || value > 255) return -1;
    event->clock = (uint64_t)clock;
    event->reg = reg;
    event->value = value;
    return 1;
}

static int next_speaker_event(FILE *input, speaker_event *event)
{
    unsigned long long clock, epoch_clk;
    unsigned port61, reload, mode;
    int n = fscanf(input, "%llu %x %u %u %llu",
                   &clock, &port61, &reload, &mode, &epoch_clk);
    if (n == EOF) return 0;
    if (n != 5 || port61 > 3 || reload > 65535 || mode > 7) return -1;
    event->clock = (uint64_t)clock;
    event->epoch_clk = (uint64_t)epoch_clk;
    event->port61 = (uint8_t)port61;
    event->reload = (uint16_t)reload;
    event->mode = (uint8_t)mode;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: audio_render OPL.LOG OUT.WAV IPS END_CLOCK [dbopl|nuked]\n"
                        "       [--speaker-log FILE] [--step-clocks N]\n"
                        "       audio_render MIDI.LOG OUT.WAV IPS END_CLOCK --mt32 CONTROL.ROM PCM.ROM [--seed N] [--step-clocks N] [--speaker-log FILE]\n");
        return 2;
    }
    uint64_t ips = strtoull(argv[3], NULL, 10), end = strtoull(argv[4], NULL, 10);
    if (ips < 100 || !end) return 2;
    uint64_t step_clocks = ips / 100;
    if (!step_clocks) return 2;

    int midi = argc > 5 && !strcmp(argv[5], "--mt32");
    audio_opl_backend backend = AUDIO_OPL_DBOPL;
    const char *speaker_path = NULL, *control = NULL, *pcm = NULL;
    unsigned int seed = 0;
    int have_seed = 0;
    if (midi) {
        if (argc < 8) return 2;
        control = argv[6];
        pcm = argv[7];
        for (int i = 8; i < argc;) {
            if (i + 1 >= argc) return 2;
            char *parse_end = NULL;
            if (!strcmp(argv[i], "--seed") && !have_seed) {
                unsigned long value = strtoul(argv[i + 1], &parse_end, 0);
                if (parse_end == argv[i + 1] || *parse_end) return 2;
                seed = (unsigned int)value;
                have_seed = 1;
            } else if (!strcmp(argv[i], "--step-clocks")) {
                unsigned long long value = strtoull(argv[i + 1], &parse_end, 10);
                if (parse_end == argv[i + 1] || *parse_end || !value) return 2;
                step_clocks = (uint64_t)value;
            } else if (!strcmp(argv[i], "--speaker-log") && !speaker_path) {
                speaker_path = argv[i + 1];
            } else return 2;
            i += 2;
        }
    } else {
        int have_backend = 0;
        for (int i = 5; i < argc;) {
            if ((!strcmp(argv[i], "dbopl") || !strcmp(argv[i], "nuked")) && !have_backend) {
                backend = !strcmp(argv[i], "nuked") ? AUDIO_OPL_NUKED : AUDIO_OPL_DBOPL;
                have_backend = 1;
                i++;
            } else if (!strcmp(argv[i], "--speaker-log") && i + 1 < argc && !speaker_path) {
                speaker_path = argv[i + 1];
                i += 2;
            } else if (!strcmp(argv[i], "--step-clocks") && i + 1 < argc) {
                char *parse_end = NULL;
                unsigned long long value = strtoull(argv[i + 1], &parse_end, 10);
                if (parse_end == argv[i + 1] || *parse_end || !value) return 2;
                step_clocks = (uint64_t)value;
                i += 2;
            } else return 2;
        }
    }

    FILE *input = fopen(argv[1], "r"), *out = fopen(argv[2], "wb");
    FILE *speaker_input = speaker_path ? fopen(speaker_path, "r") : NULL;
    if (!input || !out || (speaker_path && !speaker_input)) {
        perror("audio files");
        if (input) fclose(input);
        if (out) fclose(out);
        if (speaker_input) fclose(speaker_input);
        return 1;
    }
    audio_t *audio = audio_create_backend(ips, backend);
    if (!audio) { fclose(input); fclose(out); if (speaker_input) fclose(speaker_input); return 1; }
    if (midi) {
        char error[256];
        if (have_seed) srand(seed);
        if (!audio_enable_mt32(audio, control, pcm, error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            audio_destroy(audio); fclose(input); fclose(out); if (speaker_input) fclose(speaker_input); return 1;
        }
    }
    fwrite("RIFF", 1, 4, out); le32(out, 0); fwrite("WAVEfmt ", 1, 8, out);
    le32(out, 16); fwrite("\1\0\2\0", 1, 4, out); le32(out, AUDIO_RATE);
    le32(out, AUDIO_RATE * 4); fwrite("\4\0\20\0data", 1, 8, out); le32(out, 0);
    uint64_t at = 0, frames = 0;
    audio_event event = { 0 };
    speaker_event speaker = { 0 };
    int have_event = next_audio_event(input, midi, &event);
    int have_speaker = speaker_input ? next_speaker_event(speaker_input, &speaker) : 0;
    int rc = have_event < 0 || have_speaker < 0;
    while (!rc && (have_event || have_speaker)) {
        int take_audio = have_event && (!have_speaker || event.clock <= speaker.clock);
        uint64_t clock = take_audio ? event.clock : speaker.clock;
        if (clock < at) { rc = 1; break; }
        if (clock > end) break;
        if (!advance(audio, out, &at, clock, step_clocks, &frames)) { rc = 1; break; }
        if (take_audio) {
            if (midi) {
                if (!audio_midi_byte(audio, clock, (uint8_t)event.value)) { rc = 1; break; }
            } else audio_opl_write(audio, clock, (uint8_t)event.reg, (uint8_t)event.value);
            have_event = next_audio_event(input, midi, &event);
            if (have_event < 0) rc = 1;
        } else {
            audio_speaker_event(audio, clock, speaker.port61, speaker.reload,
                                speaker.mode, speaker.epoch_clk);
            have_speaker = next_speaker_event(speaker_input, &speaker);
            if (have_speaker < 0) rc = 1;
        }
    }
    if (!rc && !advance(audio, out, &at, end, step_clocks, &frames)) rc = 1;
    if (frames > (0xFFFFFFFFu - 36u) / 4u) rc = 1;
    if (fseek(out, 4, SEEK_SET)) rc = 1;
    le32(out, (unsigned long)(36 + frames * 4));
    if (fseek(out, 40, SEEK_SET)) rc = 1;
    le32(out, (unsigned long)(frames * 4));
    if (fclose(out)) rc = 1;
    fclose(input); audio_destroy(audio);
    if (speaker_input) fclose(speaker_input);
    printf("%llu frames at %d Hz%s\n", (unsigned long long)frames, AUDIO_RATE, rc ? "; FAILED" : "");
    return rc;
}
