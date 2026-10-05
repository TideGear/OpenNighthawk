/* matched.h - Phase 2's hand-written equivalents (see matched.c). */
#ifndef F117R_MATCHED_H
#define F117R_MATCHED_H

#include "recomp_rt.h"

/* Register every matched routine (once; recomp_init calls it). */
void matched_register(void);
unsigned matched_count(void);
const recomp_override *matched_entry(unsigned i);

#endif
