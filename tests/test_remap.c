/* test_remap.c - keyboard remapping: pairs by SDL key name, one level only, bad names rejected whole. */
#include "remap.h"

#include <SDL3/SDL_keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    CHECK(remap_add("Q:A, Nosuchkey:B")==-1 && remap_apply(SDL_SCANCODE_Q)==SDL_SCANCODE_E);
    CHECK(remap_set(SDL_SCANCODE_SEMICOLON,SDL_SCANCODE_COMMA));
    CHECK(remap_set(SDL_SCANCODE_COMMA,SDL_SCANCODE_ESCAPE));
    char *saved=remap_export(); CHECK(saved && strstr(saved,"Scan"));
    remap_clear(); CHECK(remap_add(saved)>0);free(saved);
    CHECK(remap_apply(SDL_SCANCODE_SEMICOLON)==SDL_SCANCODE_COMMA);
    CHECK(remap_apply(SDL_SCANCODE_COMMA)==SDL_SCANCODE_ESCAPE);
    CHECK(remap_set(SDL_SCANCODE_COMMA,SDL_SCANCODE_COMMA));
    CHECK(remap_apply(SDL_SCANCODE_COMMA)==SDL_SCANCODE_COMMA);
    /* More than 64 named keys must survive a canonical save/reload. */
    remap_clear(); int count=0;
    for(int id=1;id<SDL_SCANCODE_COUNT;id++) if(*SDL_GetScancodeName((SDL_Scancode)id) && id!=SDL_SCANCODE_A) {
        CHECK(remap_set((SDL_Scancode)id,SDL_SCANCODE_A));++count;
    }
    CHECK(count>64);saved=remap_export();CHECK(saved && strlen(saved)>400);
    remap_clear();CHECK(remap_add(saved)==count);free(saved);
    CHECK(remap_apply(SDL_SCANCODE_SEMICOLON)==SDL_SCANCODE_A);
    remap_clear();
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_Q);
    /* The Keys tab's assignment: a swap, the old key freed, restorable. */
    CHECK(remap_source(SDL_SCANCODE_1) == SDL_SCANCODE_1);
    CHECK(remap_assign(SDL_SCANCODE_Q, SDL_SCANCODE_1));
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_1 && remap_apply(SDL_SCANCODE_1) == SDL_SCANCODE_Q);
    CHECK(remap_source(SDL_SCANCODE_1) == SDL_SCANCODE_Q);
    CHECK(remap_assign(SDL_SCANCODE_W, SDL_SCANCODE_1));                      /* Q goes back to itself */
    CHECK(remap_apply(SDL_SCANCODE_Q) == SDL_SCANCODE_Q && remap_apply(SDL_SCANCODE_W) == SDL_SCANCODE_1 &&
          remap_apply(SDL_SCANCODE_1) == SDL_SCANCODE_W && remap_source(SDL_SCANCODE_1) == SDL_SCANCODE_W);
    CHECK(remap_assign(SDL_SCANCODE_1, SDL_SCANCODE_1));                      /* default again: no pairs left */
    { char *none = remap_export(); CHECK(none && !*none); free(none); }
    CHECK(remap_source(SDL_SCANCODE_1) == SDL_SCANCODE_1);
    puts("keyboard remapping passes");
    return 0;
}
