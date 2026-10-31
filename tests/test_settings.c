/* test_settings.c - the in-game settings menu: values from f117a.ini, keys, write-back, drawing. */
#include "settings.h"

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int find(const char *key)
{
    int n;
    const setting_def *d = settings_defs(&n);
    for (int i = 0; i < n; i++) if (!strcmp(d[i].key, key)) return i;
    return -1;
}

int main(void)
{
    settings_state s;
    int n;
    settings_defs(&n);
    const int pres = find("presentation"), fixes = find("fixes"), full = find("fullscreen"), scale = find("scale");
    CHECK(pres >= 0 && fixes >= 0 && full >= 0 && scale >= 0 && n <= 32);

    /* values come from the file; absent keys take the game's defaults */
    settings_load(&s, "; mine\npresentation = Enhanced\nfullscreen = true\nscale = 4\n");
    CHECK(!strcmp(settings_value(&s, pres), "enhanced"));
    CHECK(!strcmp(settings_value(&s, fixes), "recommended"));
    CHECK(!strcmp(settings_value(&s, full), "yes"));
    CHECK(!strcmp(settings_value(&s, scale), "4"));
    CHECK(!s.open && !settings_key_event(&s, SKEY_DOWN));     /* closed: keys belong to the game */

    /* navigating and changing; choices wrap */
    settings_open(&s);
    CHECK(s.open && s.cursor == 0);
    CHECK(settings_key_event(&s, SKEY_RIGHT));                /* presentation: enhanced -> remastered -> original */
    CHECK(!strcmp(settings_value(&s, pres), "remastered"));
    CHECK(settings_key_event(&s, SKEY_RIGHT));
    CHECK(!strcmp(settings_value(&s, pres), "original"));
    CHECK(settings_key_event(&s, SKEY_LEFT));
    CHECK(!strcmp(settings_value(&s, pres), "remastered"));
    CHECK(settings_key_event(&s, SKEY_LEFT));
    CHECK(!strcmp(settings_value(&s, pres), "enhanced"));
    CHECK(!settings_needs_restart(&s));
    CHECK(settings_key_event(&s, SKEY_UP) && s.cursor == n + 6);   /* wraps to the last row */
    CHECK(s.top == n + 7 - 12 && s.cursor >= s.top && s.cursor < s.top + 12);   /* the list scrolled to show it */
    for (int k = 0; k < n + 6; k++) settings_key_event(&s, SKEY_UP);
    CHECK(s.cursor == 0 && s.top == 0);                       /* and back to the top */
    settings_key_event(&s, SKEY_UP);
    CHECK(s.cursor == n + 6);
    /* Remap a key and Clear key remaps ask the host and keep the menu open */
    s.cursor = n + 2;
    settings_key_event(&s, SKEY_ENTER);
    CHECK(s.open && s.remap_stage == 1 && !s.clear_remaps);
    s.remap_stage = 0;
    s.cursor = n + 3;
    settings_key_event(&s, SKEY_ENTER);
    CHECK(s.open && s.clear_remaps && !s.remap_stage);
    s.clear_remaps = 0;
    /* the fix picker: the host lists the fixes, Enter opens it, Enter switches one, Esc comes back with the change noted */
    s.cursor = n + 4;
    settings_key_event(&s, SKEY_ENTER);
    CHECK(!s.picker);                                          /* no list from the host: nothing to pick */
    settings_set_fix(&s, 0, "D4", "secret airstrips outside the Gulf", 1);
    settings_set_fix(&s, 1, "Q12b", "projectile map markers are always 2x2", 0);
    CHECK(s.pick_count == 2);
    settings_key_event(&s, SKEY_ENTER);
    CHECK(s.open && s.picker && s.pick_cursor == 0 && !s.picks_changed);
    settings_key_event(&s, SKEY_ENTER);                        /* switch D4 off */
    CHECK(!s.pick_on[0] && s.picks_changed);
    settings_key_event(&s, SKEY_DOWN);
    settings_key_event(&s, SKEY_RIGHT);                        /* switch Q12b on */
    CHECK(s.pick_on[1] && s.pick_cursor == 1);
    settings_key_event(&s, SKEY_DOWN);                         /* wraps within the list */
    CHECK(s.pick_cursor == 0);
    settings_key_event(&s, SKEY_ESC);
    CHECK(s.open && !s.picker && s.picks_changed);
    settings_mark_saved(&s, "fixes", "custom");
    CHECK(!strcmp(settings_value(&s, fixes), "custom") && !settings_needs_restart(&s));
    /* Watch a flight: nothing listed means nothing to open; with a list, Enter picks one and closes the menu */
    s.cursor = n + 5;
    settings_key_event(&s, SKEY_ENTER);
    CHECK(!s.flight_picker && s.open);
    settings_set_flight(&s, 0, "session-20261024-230019", "24 Oct 2026 23:00, 312 KB");
    settings_set_flight(&s, 1, "session-20261024-191045", "24 Oct 2026 19:10, 40 KB");
    settings_key_event(&s, SKEY_ENTER);
    CHECK(s.flight_picker && s.pick_cursor == 0);
    settings_key_event(&s, SKEY_DOWN);
    settings_key_event(&s, SKEY_ESC);
    CHECK(!s.flight_picker && s.open && !s.watch_now);        /* Esc comes back without choosing */
    settings_key_event(&s, SKEY_ENTER);
    settings_key_event(&s, SKEY_DOWN);
    settings_key_event(&s, SKEY_ENTER);
    CHECK(!s.open && s.watch_now && !strcmp(s.watch_name, "session-20261024-191045"));
    settings_load(&s, "presentation = enhanced\nfullscreen = true\nscale = 4\n");
    settings_open(&s);
    s.cursor = n + 1;                                         /* Watch last flight: asks the host, drops edits */
    settings_key_event(&s, SKEY_ENTER);
    CHECK(!s.open && s.watch_now && !s.saved_now && !settings_needs_restart(&s));
    settings_load(&s, "presentation = enhanced\nfullscreen = true\nscale = 4\n");
    settings_open(&s);
    s.cursor = pres;
    settings_key_event(&s, SKEY_ENTER);                       /* Enter steps forward */
    CHECK(!strcmp(settings_value(&s, pres), "remastered") && settings_needs_restart(&s));

    /* a live setting is flagged, and does not need a restart by itself */
    s.cursor = full;
    settings_key_event(&s, SKEY_LEFT);
    CHECK(s.changed_live && !strcmp(settings_value(&s, full), "no"));

    /* the write-back changes only what changed, and keeps the file's comments */
    const char *file = "; mine\npresentation = enhanced\nfullscreen = true\nscale = 4\n";
    char *out = settings_apply(&s, file);
    CHECK(out);
    char v[32];
    CHECK(config_get(out, "presentation", v, sizeof v) && !strcmp(v, "remastered"));
    CHECK(config_get(out, "fullscreen", v, sizeof v) && !strcmp(v, "no"));
    CHECK(config_get(out, "scale", v, sizeof v) && !strcmp(v, "4"));
    CHECK(!config_get(out, "fixes", v, sizeof v));            /* untouched: not written */
    CHECK(strstr(out, "; mine"));
    free(out);

    /* Save closes and asks for a restart; Escape throws the edits away */
    s.cursor = n;
    settings_key_event(&s, SKEY_ENTER);
    CHECK(!s.open && s.want_restart && s.saved_now);
    settings_load(&s, file);
    settings_open(&s);
    settings_key_event(&s, SKEY_RIGHT);
    CHECK(settings_key_event(&s, SKEY_ESC) && !s.open);
    CHECK(!strcmp(settings_value(&s, pres), "enhanced") && !settings_needs_restart(&s));

    /* a live setting shows the window's state; Escape puts the window back as it was, not as the file has it */
    settings_load(&s, "fullscreen = no\n");
    settings_open(&s);
    settings_set_live(&s, "fullscreen", 1);                   /* the player had pressed Alt+Enter */
    CHECK(!strcmp(settings_value(&s, full), "yes"));
    s.cursor = full;
    settings_key_event(&s, SKEY_RIGHT);                       /* to "no" */
    CHECK(s.changed_live);
    s.changed_live = 0;
    settings_key_event(&s, SKEY_ESC);
    CHECK(s.changed_live && !strcmp(settings_value(&s, full), "yes"));
    settings_load(&s, file);

    /* drawing marks pixels inside the panel only */
    enum { W = 640, H = 400 };
    uint32_t *px = (uint32_t *)calloc((size_t)W * H, 4);
    CHECK(px);
    settings_open(&s);
    settings_render(&s, px, W, H);
    int lit = 0, outside = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (px[y * W + x]) { lit++; if (x < (W - 400) / 2 || x >= (W + 400) / 2) outside++; }
    CHECK(lit > 5000 && !outside);
    free(px);
    puts("settings menu passes");
    return 0;
}
