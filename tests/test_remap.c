/* test_remap.c - keyboard remapping: pairs by SDL key name, one level only, bad names rejected whole. */
#include "remap.h"

#include <SDL3/SDL_keyboard.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_Q);                 /* nothing set: unchanged */
    CHECK(remap_add("Q:W") == 1);
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_W);
    CHECK(remap_apply(SDL_SCANCODE_W) == SDL_SCANCODE_W);                  /* one way only */
    CHECK(remap_add("W:Q, A : S;Left Shift:Space") == 3);
    CHECK(remap_apply(SDL_SCANCODE_W) == SDL_SCANCODE_Q);
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_W);                  /* a swap, not a chain */
    CHECK(remap_apply(SDL_SCANCODE_A) == SDL_SCANCODE_S);
    CHECK(remap_apply(SDL_SCANCODE_LSHIFT) == SDL_SCANCODE_SPACE);
    CHECK(remap_add("Q:E") == 1 && remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_E);   /* the later pair wins */
    CHECK(remap_add("Z:X, Nosuchkey:A") == -1);                            /* a bad name keeps none of the spec */
    CHECK(remap_apply(SDL_SCANCODE_Z) == SDL_SCANCODE_Z);
    CHECK(remap_add("Z") == -1 && remap_add("") == 0);
    remap_clear();
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_Q);
    puts("keyboard remapping passes");
    return 0;
}
