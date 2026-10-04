#ifndef F117R_DBOPL_BRIDGE_H
#define F117R_DBOPL_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct dbopl dbopl_t;
dbopl_t *dbopl_create(uint32_t rate);
void dbopl_destroy(dbopl_t *chip);
void dbopl_write(dbopl_t *chip, uint8_t reg, uint8_t value);
/* OPL2 mono samples, before the DOSBox mixer gain and clipping. */
void dbopl_generate(dbopl_t *chip, int32_t *out, size_t frames);
#ifdef __cplusplus
}
#endif
#endif
