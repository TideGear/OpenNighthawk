/* settings.c - the in-game settings menu (see settings.h). */
#include "settings.h"

#include "config.h"
#include "font8x16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const C_PRESENTATION[] = { "original", "enhanced", NULL };
static const char *const C_FIXES[] = { "original", "recommended", NULL };
static const char *const C_SCALE[] = { "1", "2", "3", "4", "5", "6", NULL };
static const char *const C_YESNO[] = { "no", "yes", NULL };
static const char *const C_SPEAKER[] = { "realsound", "pwm", NULL };
static const char *const C_TIMING[] = { "dosbox", "386", NULL };
static const char *const C_ROLAND[] = { "munt", "windows", "off", NULL };
static const char *const C_HZ[] = { "8", "15", NULL };
static const char *const C_OPL[] = { "dbopl", "nuked", NULL };
static const char *const C_WIDTH[] = { "0", "1", "2", "3", "4", NULL };
static const char *const C_AGE[] = { "interp", "extrapolate", NULL };
static const char *const C_DEAD[] = { "0", "5", "10", "15", "20", "25", "30", NULL };
static const char *const C_ENGINE[] = { "recomp", "interp", NULL };

/* Fullscreen is live (Alt+Enter does the same); the rest are read when the game starts. */
static const setting_def DEFS[] = {
    { "presentation", "Presentation",     C_PRESENTATION, 0, 0 },
    { "fixes",        "Fixes",            C_FIXES,        1, 0 },
    { "scale",        "Window size",      C_SCALE,        2, 0 },
    { "fullscreen",   "Fullscreen",       C_YESNO,        0, 1 },
    { "speaker",      "Speaker speech",   C_SPEAKER,      0, 0 },
    { "timing",       "Machine speed",    C_TIMING,       0, 0 },
    { "crt",          "CRT scanlines",    C_YESNO,        0, 0 },
    { "roland",       "Roland output",    C_ROLAND,       2, 0 },
    { "sim-hz",       "Game steps a sec", C_HZ,           1, 0 },
    { "opl",          "AdLib emulation",  C_OPL,          0, 0 },
    { "no-aspect",    "Ignore 4:3 shape", C_YESNO,        0, 0 },
    { "present-line-width", "Line width",  C_WIDTH,        0, 0 },
    { "present-age",  "Picture delay",    C_AGE,          0, 0 },
    { "pad-deadzone", "Stick dead zone %", C_DEAD,        0, 0 },
    { "pad-invert-x", "Invert stick X",   C_YESNO,        0, 0 },
    { "pad-invert-y", "Invert stick Y",   C_YESNO,        0, 0 },
    { "engine",       "CPU engine",       C_ENGINE,       0, 0 },
};
#define NDEFS ((int)(sizeof DEFS / sizeof DEFS[0]))
#define SETTINGS_VISIBLE 12                           /* rows shown at once; the list scrolls past them */

const setting_def *settings_defs(int *count)
{
    if (count) *count = NDEFS;
    return DEFS;
}

static int index_of(const setting_def *d, const char *text)
{
    for (int i = 0; d->choices[i]; i++)
        if (!strcmp(d->choices[i], text)) return i;
    return -1;
}

static int count_of(const setting_def *d)
{
    int n = 0;
    while (d->choices[n]) n++;
    return n;
}

void settings_load(settings_state *s, const char *ini_text)
{
    memset(s, 0, sizeof *s);
    for (int i = 0; i < NDEFS; i++) {
        char v[64];
        int at = -1;
        if (config_get(ini_text, DEFS[i].key, v, sizeof v)) {
            for (char *p = v; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
            if (DEFS[i].choices == C_YESNO && (!strcmp(v, "true") || !strcmp(v, "on") || !strcmp(v, "1"))) snprintf(v, sizeof v, "yes");
            if (DEFS[i].choices == C_YESNO && (!strcmp(v, "false") || !strcmp(v, "off") || !strcmp(v, "0"))) snprintf(v, sizeof v, "no");
            at = index_of(&DEFS[i], v);
        }
        s->value[i] = s->saved[i] = at >= 0 ? at : DEFS[i].def;
    }
}

void settings_open(settings_state *s)
{
    s->open = 1;
    s->cursor = 0;
    s->top = 0;
    s->want_restart = 0;
    s->saved_now = 0;
    s->watch_now = 0;
    memcpy(s->at_open, s->value, sizeof s->at_open);
}

void settings_set_live(settings_state *s, const char *key, int choice)
{
    for (int i = 0; i < NDEFS; i++)
        if (DEFS[i].live && !strcmp(DEFS[i].key, key)) s->value[i] = s->at_open[i] = choice;
}

/* Close without saving: restart-only settings go back to the file's, live ones to how they were
 * when the menu opened; the host is told if a live one has to be put back. */
static void revert(settings_state *s)
{
    for (int i = 0; i < NDEFS; i++) {
        const int back = DEFS[i].live ? s->at_open[i] : s->saved[i];
        if (DEFS[i].live && s->value[i] != back) s->changed_live = 1;
        s->value[i] = back;
    }
    s->open = 0;
}

int settings_needs_restart(const settings_state *s)
{
    for (int i = 0; i < NDEFS; i++)
        if (!DEFS[i].live && s->value[i] != s->saved[i]) return 1;
    return 0;
}

const char *settings_value(const settings_state *s, int i)
{
    return DEFS[i].choices[s->value[i]];
}

int settings_key_event(settings_state *s, settings_key k)
{
    if (!s->open) return 0;
    const int rows = NDEFS + 3;                    /* the settings, "Save", "Watch last flight", "Close" */
    switch (k) {
    case SKEY_UP: s->cursor = (s->cursor + rows - 1) % rows; break;
    case SKEY_DOWN: s->cursor = (s->cursor + 1) % rows; break;
    case SKEY_LEFT: case SKEY_RIGHT:
        if (s->cursor < NDEFS) {
            const int n = count_of(&DEFS[s->cursor]);
            s->value[s->cursor] = (s->value[s->cursor] + (k == SKEY_RIGHT ? 1 : n - 1)) % n;
            if (DEFS[s->cursor].live) s->changed_live = 1;
        }
        break;
    case SKEY_ENTER:
        if (s->cursor < NDEFS) {                   /* Enter steps forward, as Right */
            return settings_key_event(s, SKEY_RIGHT);
        } else if (s->cursor == NDEFS) {           /* Save */
            s->saved_now = 1;
            s->want_restart = settings_needs_restart(s);
            s->open = 0;
        } else if (s->cursor == NDEFS + 1) {       /* Watch last flight: edits are dropped, as at Close */
            revert(s);
            s->watch_now = 1;
        } else revert(s);                          /* Close, without saving */
        break;
    case SKEY_ESC:
        revert(s);
        break;
    }
    if (s->cursor < s->top) s->top = s->cursor;                  /* keep the cursor inside the rows shown */
    if (s->cursor >= s->top + SETTINGS_VISIBLE) s->top = s->cursor - SETTINGS_VISIBLE + 1;
    return 1;
}

char *settings_apply(const settings_state *s, const char *ini_text)
{
    char *text = (char *)malloc(strlen(ini_text ? ini_text : "") + 1);
    if (!text) return NULL;
    memcpy(text, ini_text ? ini_text : "", strlen(ini_text ? ini_text : "") + 1);
    for (int i = 0; i < NDEFS; i++) {
        if (s->value[i] == s->saved[i]) continue;
        char *next = config_set(text, DEFS[i].key, DEFS[i].choices[s->value[i]]);
        free(text);
        if (!next) return NULL;
        text = next;
    }
    return text;
}

/* ---- drawing ---- */

static void put_char(uint32_t *px, int w, int h, int x, int y, unsigned char ch, uint32_t color)
{
    for (int r = 0; r < 16; r++) {
        const unsigned char bits = FONT8X16[ch][r];
        for (int c = 0; c < 8; c++)
            if ((bits & (0x80 >> c)) && x + c >= 0 && x + c < w && y + r >= 0 && y + r < h)
                px[(size_t)(y + r) * (size_t)w + (size_t)(x + c)] = color;
    }
}

static void put_text(uint32_t *px, int w, int h, int x, int y, const char *t, uint32_t color)
{
    for (; *t; t++, x += 8) put_char(px, w, h, x, y, (unsigned char)*t, color);
}

void settings_render(const settings_state *s, uint32_t *px, int w, int h)
{
    const int total = NDEFS + 3, shown = total < SETTINGS_VISIBLE ? total : SETTINGS_VISIBLE;
    const int pw = 400, ph = 24 + shown * 20 + 56;
    const int x0 = (w - pw) / 2, y0 = (h - ph) / 2;
    for (int y = y0; y < y0 + ph; y++)
        for (int x = x0; x < x0 + pw; x++) {
            const int edge = x < x0 + 2 || x >= x0 + pw - 2 || y < y0 + 2 || y >= y0 + ph - 2;
            px[(size_t)y * (size_t)w + (size_t)x] = edge ? 0xFFA0A0A0u : 0xFF101820u;
        }
    put_text(px, w, h, x0 + 16, y0 + 10, "SETTINGS", 0xFFFFFFFFu);
    for (int i = s->top; i < s->top + shown && i < total; i++) {
        const int y = y0 + 34 + (i - s->top) * 20, on = i == s->cursor;
        const uint32_t color = on ? 0xFFFFE060u : 0xFFD0D0D0u;
        if (on) put_text(px, w, h, x0 + 12, y, ">", color);
        if (i < NDEFS) {
            char v[64];
            snprintf(v, sizeof v, "< %s >", DEFS[i].choices[s->value[i]]);
            put_text(px, w, h, x0 + 30, y, DEFS[i].label, color);
            put_text(px, w, h, x0 + 200, y, v, color);
            if (!DEFS[i].live && s->value[i] != s->saved[i]) put_text(px, w, h, x0 + 370, y, "*", 0xFF60E060u);
        } else put_text(px, w, h, x0 + 30, y, i == NDEFS ? "Save" : i == NDEFS + 1 ? "Watch last flight" : "Close without saving", color);
    }
    if (s->top > 0) put_text(px, w, h, x0 + pw - 28, y0 + 10, "^", 0xFF909090u);                  /* more above */
    if (s->top + shown < total) put_text(px, w, h, x0 + pw - 28, y0 + ph - 54, "v", 0xFF909090u); /* more below */
    const int fy = y0 + ph - 38;
    put_text(px, w, h, x0 + 16, fy, "Up/Down choose, Left/Right change,", 0xFF909090u);
    put_text(px, w, h, x0 + 16, fy + 16, "* applies after restart. Esc closes.", 0xFF909090u);
}
