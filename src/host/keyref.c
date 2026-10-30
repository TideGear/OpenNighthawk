/* keyref.c - the on-screen key reference (see keyref.h). */
#include "keyref.h"

#include "font8x16.h"

#include <stdio.h>
#include <string.h>

/* Four pages, one column each: a heading has what == NULL. From the manual's Key Controls Summary. */
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
    { "Increase", "=" },
    { "Decrease", "-" },
    { "No power", "Shift -" },
    { NULL, "WEAPONS" },
    { "Select weapon", "space bar" },
    { "Fire weapon", "Return / Enter, joy button 2" },
    { "Fire cannon", "Backspace, joy button 1" },
};
static const keyref_row PAGE1[] = {
    { NULL, "DEFENSES" },
    { "Drop flare", "1" },
    { "Drop chaff", "2" },
    { "IR jammer on/off", "3" },
    { "ECM on/off", "4" },
    { "Drop decoy", "5" },
    { NULL, "EQUIPMENT" },
    { "Gear up/down", "6" },
    { "Autopilot on/off", "7" },
    { "Bay open/closed", "8" },
    { "Flaps in/out", "9" },
    { "Brakes on/off", "0" },
    { "Eject", "Shift F10" },
};
static const keyref_row PAGE2[] = {
    { NULL, "DISPLAYS" },
    { "HUD modes", "F2" },
    { "HUD de-clutter", "V" },
    { "Maps", "F3" },
    { "Day/night HUD", "F4" },
    { "Ordnance", "F5" },
    { "FLIR on/off", "F6" },
    { "ILS on/off", "F9" },
    { "Mission orders", "F10" },
    { NULL, "TRACKING CAMERA (RIGHT MFD)" },
    { "Cam ahead", "/" },
    { "Cam rear", ">" },
    { "Cam left", "<   (see note)" },
    { "Cam right", "M   (see note)" },
    { "Designate new target", "N" },
    { "Select target", "B" },
};
static const keyref_row PAGE3[] = {
    { NULL, "INS WAYPOINTS" },
    { "Select waypoint", "F7" },
    { "Change waypoint", "F8" },
    { "Reset waypoint", "Shift F8" },
    { "Last / next waypoint", "PgUp* / PgDn*" },
    { "Move waypoint", "arrows or keypad 8 2 4 6*" },
    { NULL, "FROM THE COCKPIT" },
    { "Cockpit view", "F1" },
    { "View ahead / rear", "Shift /  and  Shift >" },
    { "View left / right", "Shift <  and  Shift M" },
    { NULL, "OUT OF THE PLANE" },
    { "Slot / chase view", "Shift F1 / Shift F2" },
    { "Side / missile view", "Shift F3 / Shift F4" },
    { "Tactical / inverse", "Shift F5 / Shift F6" },
};
static const char *const NOTES[KEYREF_PAGES] = {
    "Joystick, mouse, arrow keys or the numeric keypad all steer.",
    "",
    "Note: the manual's appendix lists Cam left and right the other way round from its text; the text's order is shown.",
    "* shift if not using a joystick.",
};

const keyref_row *keyref_rows(int page, int *count)
{
    static const keyref_row *const pages[KEYREF_PAGES] = { PAGE0, PAGE1, PAGE2, PAGE3 };
    static const int sizes[KEYREF_PAGES] = { (int)(sizeof PAGE0 / sizeof PAGE0[0]), (int)(sizeof PAGE1 / sizeof PAGE1[0]),
                                             (int)(sizeof PAGE2 / sizeof PAGE2[0]), (int)(sizeof PAGE3 / sizeof PAGE3[0]) };
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

void keyref_render(int page, uint32_t *px, int w, int h)
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
            put_text(px, w, h, x0 + 32, y, rows[i].what, 0xFFD0D0D0u);
            put_text(px, w, h, x0 + 32 + 8 * (KEYREF_WHAT_CHARS + 1), y, rows[i].keys, 0xFFFFE060u);
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
    put_text(px, w, h, x0 + 16, y0 + ph - 22, "Left/Right: page   Esc: close   (the manual's Key Controls Summary)", 0xFF909090u);
}
