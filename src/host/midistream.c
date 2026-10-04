/* midistream.c - see midistream.h. */
#include "midistream.h"

#include <string.h>

void midistream_init(midistream *m)
{
    memset(m, 0, sizeof *m);
}

/* Data bytes after a channel status: two, except program change and
 * channel pressure. */
static unsigned channel_len(uint8_t status)
{
    const uint8_t kind = status & 0xF0u;
    return kind == 0xC0u || kind == 0xD0u ? 1u : 2u;
}

void midistream_byte(midistream *m, uint8_t b, midistream_fn out, void *user)
{
    if (b >= 0xF8u) {                        /* real time: goes straight out */
        out(user, &b, 1);
        return;
    }
    if (b == 0xF0u) {
        m->in_sysex = 1;
        m->nsysex = 0;
        m->sysex[m->nsysex++] = b;
        m->status = 0;                       /* SysEx cancels running status */
        return;
    }
    if (m->in_sysex) {
        if (b == 0xF7u || b >= 0x80u) {
            if (m->nsysex < MIDISTREAM_SYSEX_MAX) {
                m->sysex[m->nsysex++] = 0xF7u;
                out(user, m->sysex, m->nsysex);
            } else {
                m->dropped++;
            }
            m->in_sysex = 0;
            if (b == 0xF7u) return;
            /* A status byte ends the SysEx and starts its own message. */
        } else {
            if (m->nsysex < MIDISTREAM_SYSEX_MAX) m->sysex[m->nsysex++] = b;
            else m->nsysex = MIDISTREAM_SYSEX_MAX + 1u;
            return;
        }
    }
    if (b >= 0x80u) {
        if (b >= 0xF0u) {                    /* other system common: not used */
            m->status = 0;
            out(user, &b, 1);
            return;
        }
        m->status = b;
        m->have = 0;
        m->need = channel_len(b);
        return;
    }
    if (!m->status) return;                  /* data with no status: dropped */
    m->data[m->have++] = b;
    if (m->have == m->need) {
        uint8_t msg[3] = { m->status, m->data[0], m->data[1] };
        out(user, msg, 1u + m->need);
        m->have = 0;                         /* running status continues */
    }
}
