/* settings.c - the in-game settings menu (see settings.h). */
#include "settings.h"

#include "config.h"
#include "font8x16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const C_PRESENTATION[] = { "original", "enhanced", NULL };
static const char *const C_FIXES[] = { "original", "recommended", "custom", NULL };
static const char *const C_SCALE[] = { "1", "2", "3", "4", "5", "6", NULL };
static const char *const C_YESNO[] = { "no", "yes", NULL };
static const char *const C_SPEAKER[] = { "realsound", "pwm", NULL };
static const char *const C_TIMING[] = { "dosbox", "386", NULL };
static const char *const C_SOUND[] = { "ask", "adlib", "roland", "speaker", NULL };
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
    { "subtitles",    "Radio subtitles",  C_YESNO,        0, 0 },
    { "sound",        "Sound driver",     C_SOUND,        0, 0 },
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

void settings_set_fix(settings_state *s, int index, const char *id, const char *what, int on)
{
    if (index < 0 || index >= SETTINGS_MAX_FIXES) return;
    snprintf(s->pick_id[index], sizeof s->pick_id[index], "%s", id);
    snprintf(s->pick_what[index], sizeof s->pick_what[index], "%s", what);
    s->pick_on[index] = on ? 1 : 0;
    if (index + 1 > s->pick_count) s->pick_count = index + 1;
}

void settings_set_flight(settings_state *s, int index, const char *name, const char *what)
{
    if (index < 0 || index >= 16) return;
    snprintf(s->flight_name[index], sizeof s->flight_name[index], "%s", name);
    snprintf(s->flight_what[index], sizeof s->flight_what[index], "%s", what);
    if (index + 1 > s->flight_count) s->flight_count = index + 1;
}

void settings_mark_saved(settings_state *s, const char *key, const char *value)
{
    for (int i = 0; i < NDEFS; i++)
        if (!strcmp(DEFS[i].key, key)) {
            const int at = index_of(&DEFS[i], value);
            if (at >= 0) s->value[i] = s->saved[i] = at;
        }
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
    if (s->flight_picker) {                        /* the flight list has the keys */
        const int n = s->flight_count;
        if (k == SKEY_UP) s->pick_cursor = (s->pick_cursor + n - 1) % n;
        else if (k == SKEY_DOWN) s->pick_cursor = (s->pick_cursor + 1) % n;
        else if (k == SKEY_ENTER || k == SKEY_RIGHT) {      /* watch this one: edits are dropped, as at Close */
            snprintf(s->watch_name, sizeof s->watch_name, "%s", s->flight_name[s->pick_cursor]);
            s->flight_picker = 0;
            revert(s);
            s->watch_now = 1;
        } else if (k == SKEY_ESC) s->flight_picker = 0;
        if (s->pick_cursor < s->pick_top) s->pick_top = s->pick_cursor;
        if (s->pick_cursor >= s->pick_top + SETTINGS_VISIBLE - 2) s->pick_top = s->pick_cursor - SETTINGS_VISIBLE + 3;
        return 1;
    }
    if (s->picker) {                               /* the fix picker has the keys */
        const int n = s->pick_count;
        if (k == SKEY_UP) s->pick_cursor = (s->pick_cursor + n - 1) % n;
        else if (k == SKEY_DOWN) s->pick_cursor = (s->pick_cursor + 1) % n;
        else if (k == SKEY_LEFT || k == SKEY_RIGHT || k == SKEY_ENTER) { s->pick_on[s->pick_cursor] ^= 1; s->picks_changed = 1; }
        else if (k == SKEY_ESC) s->picker = 0;
        if (s->pick_cursor < s->pick_top) s->pick_top = s->pick_cursor;
        if (s->pick_cursor >= s->pick_top + SETTINGS_VISIBLE - 2) s->pick_top = s->pick_cursor - SETTINGS_VISIBLE + 3;
        return 1;
    }
    const int rows = NDEFS + 7;                    /* the settings, then Save, Watch last flight, Remap a key, Clear key remaps, Close */
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
        } else if (s->cursor == NDEFS + 2) {       /* Remap a key: the host takes the next two key presses */
            s->remap_stage = 1;
        } else if (s->cursor == NDEFS + 3) {       /* Clear key remaps: the host empties the list at once */
            s->clear_remaps = 1;
        } else if (s->cursor == NDEFS + 4) {       /* Choose fixes: the picker, if the host listed the fixes */
            if (s->pick_count) { s->picker = 1; s->pick_cursor = 0; s->pick_top = 0; s->picks_changed = 0; }
        } else if (s->cursor == NDEFS + 5) {       /* Watch a flight: the list of recordings, if there are any */
            if (s->flight_count) { s->flight_picker = 1; s->pick_cursor = 0; s->pick_top = 0; }
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
    const int total = NDEFS + 7, shown = total < SETTINGS_VISIBLE ? total : SETTINGS_VISIBLE;
    const int pw = 400, ph = 24 + shown * 20 + 56;
    const int x0 = (w - pw) / 2, y0 = (h - ph) / 2;
    for (int y = y0; y < y0 + ph; y++)
        for (int x = x0; x < x0 + pw; x++) {
            const int edge = x < x0 + 2 || x >= x0 + pw - 2 || y < y0 + 2 || y >= y0 + ph - 2;
            px[(size_t)y * (size_t)w + (size_t)x] = edge ? 0xFFA0A0A0u : 0xFF101820u;
        }
    if (s->flight_picker) {                        /* the flight list: newest first */
        const int per = SETTINGS_VISIBLE - 2;
        put_text(px, w, h, x0 + 16, y0 + 10, "WATCH A FLIGHT", 0xFFFFFFFFu);
        for (int i = s->pick_top; i < s->pick_top + per && i < s->flight_count; i++) {
            const int y = y0 + 34 + (i - s->pick_top) * 20, on = i == s->pick_cursor;
            if (on) put_text(px, w, h, x0 + 12, y, ">", 0xFFFFE060u);
            put_text(px, w, h, x0 + 30, y, s->flight_what[i], on ? 0xFFFFE060u : 0xFFD0D0D0u);
        }
        put_text(px, w, h, x0 + 16, y0 + ph - 38, "The game restarts to replay it from its start;", 0xFF909090u);
        put_text(px, w, h, x0 + 16, y0 + ph - 22, "Enter: watch   Esc: back", 0xFF909090u);
        return;
    }
    if (s->picker) {                               /* the fix picker: a scrolling checklist */
        const int per = SETTINGS_VISIBLE - 2;
        put_text(px, w, h, x0 + 16, y0 + 10, "CHOOSE FIXES (custom set)", 0xFFFFFFFFu);
        for (int i = s->pick_top; i < s->pick_top + per && i < s->pick_count; i++) {
            const int y = y0 + 34 + (i - s->pick_top) * 20, on = i == s->pick_cursor;
            char line[64];
            snprintf(line, sizeof line, "[%c] %-4s %.32s", s->pick_on[i] ? 'x' : ' ', s->pick_id[i], s->pick_what[i]);
            if (on) put_text(px, w, h, x0 + 12, y, ">", 0xFFFFE060u);
            put_text(px, w, h, x0 + 30, y, line, on ? 0xFFFFE060u : 0xFFD0D0D0u);
        }
        /* the highlighted fix in full, two lines */
        const char *what = s->pick_what[s->pick_cursor];
        for (int line = 0; line < 2 && *what; line++) {
            char buf[48];
            int len = (int)strlen(what) > 44 ? 44 : (int)strlen(what);
            if (what[len]) while (len > 0 && what[len] != ' ') len--;
            if (len <= 0) len = 44;
            memcpy(buf, what, (size_t)len);
            buf[len] = 0;
            put_text(px, w, h, x0 + 16, y0 + ph - 54 + line * 16, buf, 0xFFA0A0A0u);
            what += len;
            while (*what == ' ') what++;
        }
        put_text(px, w, h, x0 + 16, y0 + ph - 22, "Enter: switch   Esc: back   (applies after restart)", 0xFF909090u);
        return;
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
        } else put_text(px, w, h, x0 + 30, y, i == NDEFS ? "Save" : i == NDEFS + 1 ? "Watch last flight" : i == NDEFS + 2 ? "Remap a key..." :
                  i == NDEFS + 3 ? "Clear key remaps" : i == NDEFS + 4 ? "Choose fixes..." : i == NDEFS + 5 ? "Watch a flight..." : "Close without saving", color);
    }
    if (s->top > 0) put_text(px, w, h, x0 + pw - 28, y0 + 10, "^", 0xFF909090u);                  /* more above */
    if (s->top + shown < total) put_text(px, w, h, x0 + pw - 28, y0 + ph - 54, "v", 0xFF909090u); /* more below */
    const int fy = y0 + ph - 38;
    if (s->remap_stage) {
        put_text(px, w, h, x0 + 16, fy, s->remap_stage == 1 ? "Press the key to change (Esc: cancel)" : "Press the key it should act as", 0xFFFFE060u);
        put_text(px, w, h, x0 + 16, fy + 16, s->note, 0xFF909090u);
        return;
    }
    put_text(px, w, h, x0 + 16, fy, "Up/Down choose, Left/Right change,", 0xFF909090u);
    put_text(px, w, h, x0 + 16, fy + 16, s->note[0] ? s->note : "* applies after restart. Esc closes.", 0xFF909090u);
}
