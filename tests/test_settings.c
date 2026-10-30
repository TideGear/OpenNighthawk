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
    CHECK(pres >= 0 && fixes >= 0 && full >= 0 && scale >= 0 && n <= 16);

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
    CHECK(settings_key_event(&s, SKEY_RIGHT));                /* presentation: enhanced -> original */
    CHECK(!strcmp(settings_value(&s, pres), "original"));
    CHECK(settings_key_event(&s, SKEY_LEFT));
    CHECK(!strcmp(settings_value(&s, pres), "enhanced"));
    CHECK(!settings_needs_restart(&s));
    CHECK(settings_key_event(&s, SKEY_UP) && s.cursor == n + 2);   /* wraps to the last row */
    s.cursor = n + 1;                                         /* Watch last flight: asks the host, drops edits */
    settings_key_event(&s, SKEY_ENTER);
    CHECK(!s.open && s.watch_now && !s.saved_now && !settings_needs_restart(&s));
    settings_load(&s, "presentation = enhanced\nfullscreen = true\nscale = 4\n");
    settings_open(&s);
    s.cursor = pres;
    settings_key_event(&s, SKEY_ENTER);                       /* Enter steps forward */
    CHECK(!strcmp(settings_value(&s, pres), "original") && settings_needs_restart(&s));

    /* a live setting is flagged, and does not need a restart by itself */
    s.cursor = full;
    settings_key_event(&s, SKEY_LEFT);
    CHECK(s.changed_live && !strcmp(settings_value(&s, full), "no"));

    /* the write-back changes only what changed, and keeps the file's comments */
    const char *file = "; mine\npresentation = enhanced\nfullscreen = true\nscale = 4\n";
    char *out = settings_apply(&s, file);
    CHECK(out);
    char v[32];
    CHECK(config_get(out, "presentation", v, sizeof v) && !strcmp(v, "original"));
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
