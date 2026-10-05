/* fixes.h - switchable fixes for the original game's bugs (docs/bugs.md).
 *
 * Every fix is off unless switched on: the original, bugs included, stays
 * the reference. A code fix is a run-time override (recomp_rt.h) pinned to
 * the exact shipped file it was written against.
 */
#ifndef F117R_FIXES_H
#define F117R_FIXES_H

#include <stdio.h>

/* Register every fix (off). Safe to call more than once. */
void fixes_register(void);
/* Switch a fix by its bug ID ("D5"), or "all"; the number switched. */
int  fixes_enable(const char *id, int on);
void fixes_list(FILE *f);

#endif
