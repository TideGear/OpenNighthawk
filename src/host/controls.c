/* controls.c - controller settings (see controls.h). */
#include "controls.h"

#include "config.h"

#include <stdlib.h>

void controls_defaults(controls_config *c)
{
    c->deadzone = 0;
    c->invert_x = c->invert_y = 0;
    c->fire1 = 0;
    c->fire2 = 1;
}

static int yes(const char *v)
{
    return v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || v[0] == '1' || ((v[0] == 'o' || v[0] == 'O') && (v[1] == 'n' || v[1] == 'N'));
}

static int number(const char *text, const char *key, int lo, int hi, int *out)
{
    char v[32], *end;
    if (!config_get(text, key, v, sizeof v)) return 0;
    const long n = strtol(v, &end, 10);
    if (!*v || *end || n < lo || n > hi) return 0;
    *out = (int)n;
    return 1;
}

int controls_load(controls_config *c, const char *ini_text)
{
    int n = 0;
    char v[32];
    controls_defaults(c);
    n += number(ini_text, "pad-deadzone", 0, 50, &c->deadzone);
    n += number(ini_text, "pad-fire1", 0, 31, &c->fire1);
    n += number(ini_text, "pad-fire2", 0, 31, &c->fire2);
    if (config_get(ini_text, "pad-invert-x", v, sizeof v)) { c->invert_x = yes(v); n++; }
    if (config_get(ini_text, "pad-invert-y", v, sizeof v)) { c->invert_y = yes(v); n++; }
    return n;
}

unsigned controls_axis(const controls_config *c, int raw, int invert)
{
    if (raw < -32768) raw = -32768;
    if (raw > 32767) raw = 32767;
    const long dead = 32767L * c->deadzone / 100;
    long v = raw;
    if (v > dead) v = (v - dead) * 32767L / (32767L - dead);
    else if (v < -dead) v = (v + dead) * 32767L / (32767L - dead);
    else v = 0;
    if (invert) v = -v;
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    return (unsigned)(((float)v / 32767.0f + 1.0f) * 127.5f);   /* the original expression, so the defaults change nothing */
}

unsigned controls_buttons(const controls_config *c, unsigned held_mask)
{
    return ((held_mask >> c->fire1) & 1u) | (((held_mask >> c->fire2) & 1u) << 1);
}
