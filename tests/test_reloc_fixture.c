/* Synthetic relocation fixtures; these contain no game data. */
#include <stdint.h>

const char *const RC_RELOC_NAMES[] = {
  "LOAD.EXE", "WORD.EXE", "PLAIN.COM",
};
const uint64_t RC_RELOC_HASHES[] = {
  0x0000000000000011ull, 0x0000000000000022ull, 0x0000000000000033ull,
};
const uint32_t RC_RELOC_STARTS[] = {
  0, 1, 3, 3,
};
const uint32_t RC_RELOC_OFFSETS[] = {
  1, 0, 6,
};
const unsigned RC_NRELOC_MODULES = 3;
const unsigned RC_NRELOCS = 3;
