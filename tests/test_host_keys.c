#include "host_keys.h"
#include <stdio.h>

#define CHECK(key, code) do { if (pc_scancode(key) != (code)) { fprintf(stderr, "%s did not map to %03X\n", #key, (unsigned)(code)); return 1; } } while (0)

int main(void)
{
    /* Operator keys are outside SDL's contiguous keypad-digit range. */
    CHECK(SDL_SCANCODE_KP_PLUS, 0x4E);
    CHECK(SDL_SCANCODE_KP_MINUS, 0x4A);
    CHECK(SDL_SCANCODE_KP_MULTIPLY, 0x37);
    CHECK(SDL_SCANCODE_KP_PERIOD, 0x53);
    CHECK(SDL_SCANCODE_KP_0, 0x52);
    CHECK(SDL_SCANCODE_KP_5, 0x4C);
    CHECK(SDL_SCANCODE_KP_ENTER, 0x11C);
    CHECK(SDL_SCANCODE_RETURN, 0x1C);
    CHECK(SDL_SCANCODE_KP_DIVIDE, 0x135);
    CHECK(SDL_SCANCODE_SLASH, 0x35);
    CHECK(SDL_SCANCODE_DOWN, 0x150);
    CHECK(SDL_SCANCODE_KP_2, 0x50);
    CHECK(SDL_SCANCODE_RCTRL, 0x11D);
    CHECK(SDL_SCANCODE_LCTRL, 0x1D);
    CHECK(SDL_SCANCODE_UNKNOWN, 0);
    return 0;
}
