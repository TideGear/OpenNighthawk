#include "mt32.h"
#include "midistream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef F117R_HAS_MT32EMU
#define MT32EMU_API_TYPE 1
#include <mt32emu/mt32emu.h>
struct mt32 { mt32emu_context context; midistream input; int failed; };

mt32_t *mt32_create(const char *control, const char *pcm, unsigned rate,
                    char *error, size_t error_size)
{
    mt32_t *s = (mt32_t *)calloc(1, sizeof *s);
    mt32emu_report_handler_i report = { NULL };
    int code = MT32EMU_RC_FAILED;
    if (!s) goto failed;
    s->context = mt32emu_create_context(report, NULL);
    if (!s->context) goto failed;
    if (!control || !pcm || !rate) { code = MT32EMU_RC_MISSING_ROMS; goto failed; }
    code = mt32emu_add_rom_file(s->context, control);
    if (code != MT32EMU_RC_ADDED_CONTROL_ROM) goto failed;
    code = mt32emu_add_rom_file(s->context, pcm);
    if (code != MT32EMU_RC_ADDED_PCM_ROM) goto failed;
    mt32emu_set_stereo_output_samplerate(s->context, rate);
    code = mt32emu_open_synth(s->context);
    if (code != MT32EMU_RC_OK) goto failed;
    midistream_init(&s->input);
    if (error && error_size) error[0] = 0;
    return s;
failed:
    if (error && error_size) snprintf(error, error_size, "Munt could not load the control/PCM ROM pair (code %d)", code);
    mt32_destroy(s);
    return NULL;
}

void mt32_destroy(mt32_t *s)
{
    if (s) { if (s->context) mt32emu_free_context(s->context); free(s); }
}

static void send(void *user, const uint8_t *message, size_t length)
{
    mt32_t *s = (mt32_t *)user;
    mt32emu_return_code code;
    if (message[0] == 0xf0)
        code = mt32emu_play_sysex(s->context, message, (mt32emu_bit32u)length);
    else {
        uint32_t word = message[0];
        for (size_t i = 1; i < length && i < 3; ++i) word |= (uint32_t)message[i] << (8 * i);
        code = mt32emu_play_msg(s->context, word);
    }
    if (code != MT32EMU_RC_OK) {
        s->failed = 1;
        fprintf(stderr, "Munt rejected a MIDI message (code %d)\n", code);
    }
}

int mt32_byte(mt32_t *s, uint8_t byte)
{
    unsigned long dropped = s->input.dropped;
    midistream_byte(&s->input, byte, send, s);
    if (s->input.dropped != dropped) {
        s->failed = 1;
        fprintf(stderr, "Munt MIDI SysEx exceeds the input buffer\n");
    }
    return !s->failed;
}
void mt32_render(mt32_t *s, int16_t *stereo, unsigned frames)
{ mt32emu_render_bit16s(s->context, stereo, frames); }
#else
struct mt32 { int unused; };
mt32_t *mt32_create(const char *control, const char *pcm, unsigned rate,
                    char *error, size_t error_size)
{
    (void)control; (void)pcm; (void)rate;
    if (error && error_size) snprintf(error, error_size, "This build has no Munt support; enable F117R_WITH_MT32EMU");
    return NULL;
}
void mt32_destroy(mt32_t *s) { (void)s; }
int mt32_byte(mt32_t *s, uint8_t byte) { (void)s; (void)byte; return 1; }
void mt32_render(mt32_t *s, int16_t *stereo, unsigned frames)
{ (void)s; memset(stereo, 0, frames * 2 * sizeof *stereo); }
#endif
