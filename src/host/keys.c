/* keys.c - typing text on the emulated keyboard as set-1 scancodes. */
#include "keys.h"

#include <ctype.h>
#include <string.h>

int keys_scancode(char ch, int *shift)
{
    static const char row1[] = "1234567890-=";
    static const char row1s[] = "!@#$%^&*()_+";
    static const char row2[] = "qwertyuiop[]";
    static const char row2s[] = "QWERTYUIOP{}";
    static const char row3[] = "asdfghjkl;'`";
    static const char row3s[] = "ASDFGHJKL:\"~";
    static const char row4[] = "zxcvbnm,./";
    static const char row4s[] = "ZXCVBNM<>?";
    const char *p;
    *shift = 0;
    if (!ch) return 0;
    if ((p = strchr(row1, ch))) return 0x02 + (int)(p - row1);
    if ((p = strchr(row1s, ch))) { *shift = 1; return 0x02 + (int)(p - row1s); }
    if ((p = strchr(row2, ch))) return 0x10 + (int)(p - row2);
    if ((p = strchr(row2s, ch))) { *shift = 1; return 0x10 + (int)(p - row2s); }
    if ((p = strchr(row3, ch))) return 0x1E + (int)(p - row3);
    if ((p = strchr(row3s, ch))) { *shift = 1; return 0x1E + (int)(p - row3s); }
    if ((p = strchr(row4, ch))) return 0x2C + (int)(p - row4);
    if ((p = strchr(row4s, ch))) { *shift = 1; return 0x2C + (int)(p - row4s); }
    switch (ch) {
    case '\\': return 0x2B;
    case '|': *shift = 1; return 0x2B;
    case ' ': return 0x39;
    case '\r': return 0x1C;
    case 0x1B: return 0x01;
    case '\t': return 0x0F;
    case '\b': return 0x0E;
    default: return 0;
    }
}

int keys_next(const char **sp, uint8_t make[4], int *nmake, uint8_t brk[4], int *nbrk)
{
    const char *s = *sp;
    for (; *s; s++) {
        int code = 0, shift = 0, grey = 0;
        if (*s == '\\' && s[1]) {
            s++;
            switch (*s) {
            case 'r': code = 0x1C; break;
            case 'e': code = 0x01; break;
            case 't': code = 0x0F; break;
            case 'b': code = 0x0E; break;
            case 'U': code = 0x48; grey = 1; break;
            case 'D': code = 0x50; grey = 1; break;
            case 'L': code = 0x4B; grey = 1; break;
            case 'R': code = 0x4D; grey = 1; break;
            case '0': code = 0x44; break;
            case '\\': code = 0x2B; break;
            default:
                if (*s >= '1' && *s <= '9') code = 0x3B + (*s - '1');
                break;
            }
        } else {
            code = keys_scancode(*s, &shift);
        }
        if (!code) continue;
        int nm = 0, nb = 0;
        if (shift) make[nm++] = 0x2A;
        if (grey) make[nm++] = 0xE0;
        make[nm++] = (uint8_t)code;
        if (grey) brk[nb++] = 0xE0;
        brk[nb++] = (uint8_t)(code | 0x80);
        if (shift) brk[nb++] = 0xAA;
        *nmake = nm;
        *nbrk = nb;
        *sp = s + 1;
        return 1;
    }
    *sp = s;
    return 0;
}
