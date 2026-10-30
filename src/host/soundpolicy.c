/* soundpolicy.c - which source plays a sound (see soundpolicy.h). */
#include "soundpolicy.h"

#include <string.h>

static const sp_source ORDER[SP_KINDS][4] = {
    { SP_MAC, SP_SPEAKER, SP_NONE, SP_NONE },
    { SP_MAC, SP_ROLAND, SP_ADLIB, SP_NONE },
    { SP_MAC, SP_ADLIB, SP_NONE, SP_NONE },
};

sp_source sp_choose(sp_kind kind, unsigned available, sp_source override)
{
    if (kind < 0 || kind >= SP_KINDS) return SP_NONE;
    if (override != SP_NONE && (available & SP_BIT(override))) return override;
    for (int i = 0; i < 4 && ORDER[kind][i] != SP_NONE; i++)
        if (available & SP_BIT(ORDER[kind][i])) return ORDER[kind][i];
    return SP_NONE;
}

static const char *const NAMES[] = { "default", "mac", "roland", "adlib", "speaker" };

const char *sp_source_name(sp_source s) { return s >= 0 && s <= SP_SPEAKER ? NAMES[s] : "?"; }

int sp_parse_source(const char *word)
{
    for (int i = 0; i <= SP_SPEAKER; i++)
        if (!strcmp(word, NAMES[i])) return i;
    return -1;
}
