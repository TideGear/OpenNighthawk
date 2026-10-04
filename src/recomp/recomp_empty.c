/* recomp_empty.c - the module list of a build with no generated
 * translation: the run-time registers every module untranslated and the
 * interpreter runs everything (coverage still works). */
#include "recomp_gen.h"

const rc_module *const RC_MODULES[1] = { 0 };
const unsigned RC_NMODULES = 0;
