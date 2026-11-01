/* A second DOS sound driver (hybrid sound). The game drives a sound driver through five far calls and the driver hooks no
 * interrupt, so a bare machine holding only the driver's overlay is enough: the host calls the entries (init, raise an
 * id, resync, sequencer tick, noise step) and takes the OPL writes the driver makes. See docs/sound-board.md. */
#ifndef F117R_SIDECAR_H
#define F117R_SIDECAR_H
#include <stddef.h>
#include <stdint.h>

typedef struct sidecar sidecar_t;
typedef void (*sidecar_opl_fn)(void *user, uint64_t icount, uint8_t reg, uint8_t val);

/* Stage the AdLib driver image (the whole ASOUND.117 file) and run its init. NULL when the file is not a driver this
 * understands or the init does not return. `opl` receives every register write the driver makes. */
sidecar_t *sidecar_new_adlib(const uint8_t *file, size_t len, sidecar_opl_fn opl, void *user);
void       sidecar_free(sidecar_t *s);
/* Entry 1: raise sound `id`. Returns 0 when the driver did not return within its step limit. */
int        sidecar_request(sidecar_t *s, uint16_t id);
/* One game timer tick: entry 3 (the sequencer) on six ticks in seven, entry 4 (the noise step) every tick. */
int        sidecar_tick(sidecar_t *s);
uint64_t   sidecar_icount(const sidecar_t *s);

#endif
