/* fixes.h - switchable fixes for the original game's bugs (docs/bugs.md).
 *
 * Every fix is off unless switched on: the original, bugs included, stays
 * the reference. A code fix is a run-time override (recomp_rt.h) pinned to
 * the exact shipped file it was written against.
 */
#ifndef F117R_FIXES_H
#define F117R_FIXES_H

#include "machine.h"

#include <stdio.h>

/* Register every fix (off). Safe to call more than once. */
void fixes_register(void);
/* Switch a fix by its bug ID ("D5"), or "all"; the number switched. */
int  fixes_enable(const char *id, int on);
void fixes_list(FILE *f);
/* The fixes now on, space-separated ("" for the original). */
void fixes_enabled(char *out, size_t n);
/* Switch on each fix a recorded session names; 0 if one is unknown. */
int  fixes_enable_list(const char *ids);
/* machine_hooks.file_data: the data corrections of the fixes that are on. */
void fixes_file_data(void *user, machine_t *m, const char *name, long size, long pos,
                     uint8_t *buf, size_t n);

#endif
