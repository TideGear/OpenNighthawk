/* test_soundpolicy.c - the default source order and the player's override. */
#include "soundpolicy.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    const unsigned all = SP_BIT(SP_MAC) | SP_BIT(SP_ROLAND) | SP_BIT(SP_ADLIB) | SP_BIT(SP_SPEAKER);
    /* Mac first for every kind */
    for (int k = 0; k < SP_KINDS; k++) CHECK(sp_choose((sp_kind)k, all, SP_NONE) == SP_MAC);
    /* without a Mac recording */
    const unsigned dos = all & ~SP_BIT(SP_MAC);
    CHECK(sp_choose(SP_SPEECH, dos, SP_NONE) == SP_SPEAKER);
    CHECK(sp_choose(SP_MUSIC, dos, SP_NONE) == SP_ROLAND);
    CHECK(sp_choose(SP_SOUND, dos, SP_NONE) == SP_ADLIB);
    /* music falls to AdLib when Roland cannot run; a sound with no AdLib clip has nothing (the caller says so) */
    CHECK(sp_choose(SP_MUSIC, SP_BIT(SP_ADLIB), SP_NONE) == SP_ADLIB);
    CHECK(sp_choose(SP_SOUND, SP_BIT(SP_ROLAND), SP_NONE) == SP_NONE);
    CHECK(sp_choose(SP_SPEECH, 0, SP_NONE) == SP_NONE);
    /* an override wins when available, and falls back to the default when not */
    CHECK(sp_choose(SP_MUSIC, all, SP_ADLIB) == SP_ADLIB);
    CHECK(sp_choose(SP_SOUND, dos, SP_ROLAND) == SP_ROLAND);
    CHECK(sp_choose(SP_SOUND, SP_BIT(SP_MAC) | SP_BIT(SP_ADLIB), SP_ROLAND) == SP_MAC);
    /* names */
    CHECK(sp_parse_source("mac") == SP_MAC && sp_parse_source("default") == SP_NONE && sp_parse_source("x") == -1);
    CHECK(sp_choose(SP_MUSIC, all, SP_NONE) == SP_MAC && sp_source_name(SP_SPEAKER)[0] == 's');
    puts("sound policy passes");
    return 0;
}
