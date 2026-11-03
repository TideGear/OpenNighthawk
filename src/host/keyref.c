/* keyref.c - the on-screen key reference (see keyref.h). */
#include "keyref.h"

#include "font8x16.h"

#include <stdio.h>
#include <string.h>

/* Five pages, one column each: a heading has what == NULL. The first four are from the manual's Key Controls Summary, the fifth is this
 * program's own keys. */
static const keyref_row PAGE0[] = {
    { NULL, "FLYING" },
    { "Pitch down / up", "up / down arrow, keypad 8 / 2" },
    { "Roll right / left", "right / left arrow, keypad 6 / 4" },
    { "Dive or climb, right", "keypad 9 / 3" },
    { "Dive or climb, left", "keypad 7 / 1" },
    { "Stick sensitivity", "Ins (tiny, small, medium)" },
    { "Max stick movement", "fast double-press of the key" },
    { NULL, "THROTTLE" },
    { "Maximum power", "Shift +" },
    { "Increase", "=", "=" },
    { "Decrease", "-", "-" },
    { "No power", "Shift -" },
    { NULL, "WEAPONS" },
    { "Select weapon", "space bar", "Space" },
    { "Fire weapon", "Return / Enter, joy button 2", "Return" },
    { "Fire cannon", "Backspace, joy button 1", "Backspace" },
};
static const keyref_row PAGE1[] = {
    { NULL, "DEFENSES" },
    { "Drop flare", "1", "1" },
    { "Drop chaff", "2", "2" },
    { "IR jammer on/off", "3", "3" },
    { "ECM on/off", "4", "4" },
    { "Drop decoy", "5", "5" },
    { NULL, "EQUIPMENT" },
    { "Gear up/down", "6", "6" },
    { "Autopilot on/off", "7", "7" },
    { "Bay open/closed", "8", "8" },
    { "Flaps in/out", "9", "9" },
    { "Brakes on/off", "0", "0" },
    { "Eject", "Shift F10" },
};
static const keyref_row PAGE2[] = {
    { NULL, "DISPLAYS" },
    { "HUD modes", "F2", "F2" },
    { "HUD de-clutter", "V", "V" },
    { "Maps", "F3", "F3" },
    { "Day/night HUD", "F4", "F4" },
    { "Ordnance", "F5", "F5" },
    { "FLIR on/off", "F6", "F6" },
    { "ILS on/off", "F9", "F9" },
    { "Mission orders", "F10", "F10" },
    { NULL, "TRACKING CAMERA (RIGHT MFD)" },
    { "Cam ahead", "/", "/" },
    { "Cam rear", ">" },
    { "Cam left", "<   (see note)" },
    { "Cam right", "M   (see note)", "M" },
    { "Designate new target", "N", "N" },
    { "Select target", "B", "B" },
};
static const keyref_row PAGE3[] = {
    { NULL, "INS WAYPOINTS" },
    { "Select waypoint", "F7", "F7" },
    { "Change waypoint", "F8", "F8" },
    { "Reset waypoint", "Shift F8" },
    { "Last / next waypoint", "PgUp* / PgDn*" },
    { "Move waypoint", "arrows or keypad 8 2 4 6*" },
    { NULL, "FROM THE COCKPIT" },
    { "Cockpit view", "F1", "F1" },
    { "View ahead / rear", "Shift /  and  Shift >" },
    { "View left / right", "Shift <  and  Shift M" },
    { NULL, "OUT OF THE PLANE" },
    { "Slot / chase view", "Shift F1 / Shift F2" },
    { "Side / missile view", "Shift F3 / Shift F4" },
    { "Tactical / inverse", "Shift F5 / Shift F6" },
};
static const keyref_row PAGE4[] = {
    { NULL, "THIS PROGRAM" },
    { "Menu (anywhere)", "` (tilde)" },
    { "Other tab in the menu", "Tab" },
    { "Settings menu", "Ctrl+Alt+S" },
    { "This key list", "Ctrl+Alt+K" },
    { "Turn its pages", "left / right arrow" },
    { "Close the list or menu", "`" },
    { "Pause the game", "Ctrl+Alt+P" },
    { "Full screen on / off", "Alt+Enter" },
    { "Quit", "Ctrl+Alt+F12" },
    { NULL, "WATCHING A SAVED FLIGHT" },
    { "Faster / slower", "+  and  -" },
    { "Normal speed", "0" },
    { "30 seconds on / back", "]  and  [" },
    { "Go to a second", "G, digits, Enter" },
    { "Start it again", "Ctrl+Alt+R" },
};
static const char *const NOTES[KEYREF_PAGES] = {
    "Joystick, mouse, arrow keys or the numeric keypad all steer.",
    "",
    "Note: the manual's appendix lists Cam left and right the other way round from its text; the text's order is shown.",
    "* shift if not using a joystick.",
    "The settings menu and the pause work while you fly; the last four keys only while a saved flight is watched (--watch).",
};

/* Remapping. A row with a default key (its third field) can be given another physical key. Assigning P to a row whose game key is D
 * makes P send D and D send what P sent (a swap), so no key is left sending the same thing twice. */
const char *keyref_row_default(const keyref_row *row) { return row && row->what ? row->def : NULL; }

const keyref_row *keyref_rows(int page, int *count)
{
    static const keyref_row *const pages[KEYREF_PAGES] = { PAGE0, PAGE1, PAGE2, PAGE3, PAGE4 };
    static const int sizes[KEYREF_PAGES] = { (int)(sizeof PAGE0 / sizeof PAGE0[0]), (int)(sizeof PAGE1 / sizeof PAGE1[0]),
                                             (int)(sizeof PAGE2 / sizeof PAGE2[0]), (int)(sizeof PAGE3 / sizeof PAGE3[0]),
                                             (int)(sizeof PAGE4 / sizeof PAGE4[0]) };
    if (page < 0 || page >= KEYREF_PAGES) page = 0;
    if (count) *count = sizes[page];
    return pages[page];
}

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

void keyref_render(int page, uint32_t *px, int w, int h) { keyref_render_bind(page, px, w, h, -1, 0, NULL, NULL); }

void keyref_render_bind(int page, uint32_t *px, int w, int h, int cursor, int waiting, const char *const *bound, const int *changed)
{
    int n;
    if (page < 0 || page >= KEYREF_PAGES) page = 0;
    const keyref_row *rows = keyref_rows(page, &n);
    const int pw = 560, ph = 372, x0 = (w - pw) / 2, y0 = (h - ph) / 2;
    for (int y = y0; y < y0 + ph; y++)
        for (int x = x0; x < x0 + pw; x++) {
            const int edge = x < x0 + 2 || x >= x0 + pw - 2 || y < y0 + 2 || y >= y0 + ph - 2;
            px[(size_t)y * (size_t)w + (size_t)x] = edge ? 0xFFA0A0A0u : 0xFF101820u;
        }
    char title[64];
    snprintf(title, sizeof title, "KEYS  (page %d of %d)", page + 1, KEYREF_PAGES);
    put_text(px, w, h, x0 + 16, y0 + 8, title, 0xFFFFFFFFu);
    for (int i = 0; i < n; i++) {
        const int y = y0 + 32 + i * 18;
        if (!rows[i].what) put_text(px, w, h, x0 + 16, y, rows[i].keys, 0xFF60C8D8u);
        else {
            const int kx = x0 + 32 + 8 * (KEYREF_WHAT_CHARS + 1);
            if (i == cursor) {
                for (int yy = y - 1; yy < y + 16 && yy < h; yy++) for (int xx = x0 + 6; xx < x0 + pw - 6; xx++) px[(size_t)yy * (size_t)w + (size_t)xx] = 0xFF283848u;
                put_text(px, w, h, x0 + 16, y, ">", 0xFFFFFFFFu);
            }
            put_text(px, w, h, x0 + 32, y, rows[i].what, 0xFFD0D0D0u);
            if (i == cursor && waiting) put_text(px, w, h, kx, y, "press the new key...", 0xFFFFFFFFu);
            else if (bound && bound[i] && changed && changed[i]) put_text(px, w, h, kx, y, bound[i], 0xFF60B0FFu);
            else put_text(px, w, h, kx, y, bound && bound[i] ? bound[i] : rows[i].keys, 0xFFFFE060u);
        }
    }
    /* the page's note, wrapped to the panel's width (66 characters) */
    const char *note = NOTES[page];
    for (int line = 0; *note && line < 2; line++) {
        char buf[72];
        int len = (int)strlen(note) > 66 ? 66 : (int)strlen(note);
        if (note[len]) while (len > 0 && note[len] != ' ') len--;
        if (len <= 0) len = 66;
        memcpy(buf, note, (size_t)len);
        buf[len] = 0;
        put_text(px, w, h, x0 + 16, y0 + ph - 56 + line * 16, buf, 0xFF909090u);
        note += len;
        while (*note == ' ') note++;
    }
    put_text(px, w, h, x0 + 16, y0 + ph - 22, page == KEYREF_PAGES - 1 ? "Left/Right: page   `: close" :
             "Up/Down: row  Enter: remap  Backspace: default  Left/Right: page", 0xFF909090u);
}

void keyref_tabs(int active, uint32_t *px, int w, int h)
{
    static const char *const NAME[2] = { "SETTINGS", "KEYS" };
    static const char hint[] = "Tab: switch   `: close";
    const int pw = 560, x0 = (w - pw) / 2;
    for (int y = 6; y < 30 && y < h; y++)
        for (int x = x0; x < x0 + pw && x < w; x++) px[y * w + x] = 0xF0101820u;
    for (int i = 0; i < 2; i++) {
        const int x = x0 + 16 + i * 120;
        if (i == active) for (int y = 26; y < 29; y++) for (int xx = x - 6; xx < x + 100; xx++) px[y * w + xx] = 0xFFFFE060u;
        put_text(px, w, h, x, 10, NAME[i], i == active ? 0xFFFFE060u : 0xFF909090u);
    }
    put_text(px, w, h, x0 + pw - 16 - 8 * (int)strlen(hint), 10, hint, 0xFF707070u);
}
