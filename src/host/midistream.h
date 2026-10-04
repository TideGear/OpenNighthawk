/* midistream.h - bytes on an MPU-401 in UART mode, as whole MIDI messages.
 *
 * RSOUND.117 writes the Roland's traffic one byte at a time to port 330h:
 * channel messages (with running status), Roland SysEx and nothing else.
 * A host MIDI port takes whole messages, so this reassembles them. It does
 * not interpret them: the device does that, as the MT-32 on the original's
 * MPU-401 did.
 */
#ifndef F117R_MIDISTREAM_H
#define F117R_MIDISTREAM_H

#include <stddef.h>
#include <stdint.h>

#define MIDISTREAM_SYSEX_MAX 512

typedef void (*midistream_fn)(void *user, const uint8_t *msg, size_t len);

typedef struct {
    uint8_t status;              /* running status; 0 before any */
    uint8_t data[2];
    unsigned have, need;
    uint8_t sysex[MIDISTREAM_SYSEX_MAX];
    size_t nsysex;
    int in_sysex;
    unsigned long dropped;       /* SysEx longer than the buffer */
} midistream;

void midistream_init(midistream *m);
/* Feed one byte; `out` gets each complete message (status byte first). */
void midistream_byte(midistream *m, uint8_t b, midistream_fn out, void *user);

#endif
