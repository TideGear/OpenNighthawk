/* keys.h - typing text on the emulated keyboard as set-1 scancodes. */
#ifndef F117R_KEYS_H
#define F117R_KEYS_H

#include <stdint.h>

/* The set-1 make code for an ASCII character, and whether Shift is needed.
 * Returns 0 for characters the US keyboard cannot type. Escapes understood
 * by keys_next: \r Enter, \e Esc, \t Tab, \b Backspace, \U \D \L \R
 * arrows, \1..\9 \0 F1..F10. */
int keys_scancode(char ch, int *shift);

/* Parse one key from a script string, advancing *s. `make` receives the
 * bytes of the press (Shift, E0 prefix, make code) and `brk` those of the
 * release. Returns 0 at the end of the string. */
int keys_next(const char **s, uint8_t make[4], int *nmake, uint8_t brk[4], int *nbrk);

#endif
