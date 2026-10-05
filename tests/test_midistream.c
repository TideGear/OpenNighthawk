/* Real-time bytes must not corrupt running status or a Roland SysEx. */
#include "midistream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct { unsigned count; size_t length[8]; uint8_t message[8][512]; } messages;
static void output(void *user, const uint8_t *data, size_t length)
{
    messages *out = (messages *)user;
    CHECK(out->count < 8 && length <= 512);
    out->length[out->count] = length;
    memcpy(out->message[out->count++], data, length);
}
static void feed(midistream *m, messages *out, const uint8_t *bytes, size_t count)
{ for (size_t i = 0; i < count; ++i) midistream_byte(m, bytes[i], output, out); }
int main(void)
{
    midistream m; messages out = { 0 }; midistream_init(&m);
    const uint8_t notes[] = { 60, 0x90, 60, 0xf8, 100, 62, 80, 0xc1, 5, 6 };
    feed(&m, &out, notes, sizeof notes);
    CHECK(out.count == 5 && out.length[0] == 1 && out.message[0][0] == 0xf8);
    CHECK(out.length[1] == 3 && !memcmp(out.message[1], "\x90\x3c\x64", 3));
    CHECK(out.length[2] == 3 && !memcmp(out.message[2], "\x90\x3e\x50", 3));
    CHECK(out.length[3] == 2 && !memcmp(out.message[3], "\xc1\x05", 2));
    CHECK(out.length[4] == 2 && !memcmp(out.message[4], "\xc1\x06", 2));
    memset(&out, 0, sizeof out);
    const uint8_t sysex[] = { 0xf0, 0x41, 0x10, 0xfe, 0x16, 0xf7, 1, 2 };
    feed(&m, &out, sysex, sizeof sysex);
    CHECK(out.count == 2 && out.message[0][0] == 0xfe);
    CHECK(out.length[1] == 5 && !memcmp(out.message[1], "\xf0\x41\x10\x16\xf7", 5));
    memset(&out, 0, sizeof out);
    const uint8_t interrupted[] = { 0xf0, 0x41, 0x90, 60, 100 };
    feed(&m, &out, interrupted, sizeof interrupted);
    CHECK(out.count == 2 && out.length[0] == 3 && out.message[0][2] == 0xf7);
    CHECK(out.length[1] == 3 && out.message[1][0] == 0x90);
    memset(&out, 0, sizeof out);
    midistream_byte(&m, 0xf0, output, &out);
    for (unsigned i = 0; i < 510; ++i) midistream_byte(&m, 1, output, &out);
    midistream_byte(&m, 0xf7, output, &out);
    CHECK(out.count == 1 && out.length[0] == 512 && !m.dropped);
    midistream_byte(&m, 0xf0, output, &out);
    for (unsigned i = 0; i < 512; ++i) midistream_byte(&m, 1, output, &out);
    midistream_byte(&m, 0xf7, output, &out);
    CHECK(out.count == 1 && m.dropped == 1);
    const uint8_t recover[] = { 0xd0, 42, 0x90, 60, 0x80, 61, 0 };
    feed(&m, &out, recover, sizeof recover);
    CHECK(out.count == 3 && out.length[1] == 2 && out.message[1][1] == 42);
    CHECK(out.length[2] == 3 && !memcmp(out.message[2], "\x80\x3d\0", 3));
    puts("MIDI reassembly, real-time interleaving, bounded SysEx and recovery pass");
    return 0;
}
