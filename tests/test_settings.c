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
    CHECK(pres >= 0 && fixes >= 0 && full >= 0 && scale >= 0 && n <= SETTINGS_MAX_DEFS);
    {
        const setting_def *defs = settings_defs(NULL);
        for (int i = 0; i < n; ++i) CHECK(strlen(settings_description(defs[i].key)) > 20);
    }

    /* the colour-blind aid is a menu row with its four choices, off first */
    {
        const int cb = find("colourblind");
        CHECK(cb >= 0);
        settings_load(&s, "colourblind = deuteranopia\n");
        CHECK(!strcmp(settings_value(&s, cb), "deuteranopia"));
        settings_load(&s, "");
        CHECK(!strcmp(settings_value(&s, cb), "off"));
    }
    /* the picture detail and the machine clock are rows too, with the game's own defaults when the file leaves them out */
    {
        const int ps = find("present-scale"), ips = find("ips");
        CHECK(ps >= 0 && ips >= 0);
        settings_load(&s, "");
        CHECK(!strcmp(settings_value(&s, ps), "auto") && !strcmp(settings_value(&s, ips), "60000000"));
        settings_load(&s, "present-scale = 6\nips = 9000000\n");
        CHECK(!strcmp(settings_value(&s, ps), "6") && !strcmp(settings_value(&s, ips), "9000000"));
    }
    /* the controller's two fire buttons are rows, south and east as the old fixed mapping had them */
    {
        const int f1 = find("pad-fire1"), f2 = find("pad-fire2");
        CHECK(f1 >= 0 && f2 >= 0);
        settings_load(&s, "");
        CHECK(!strcmp(settings_value(&s, f1), "0") && !strcmp(settings_value(&s, f2), "1"));
        settings_load(&s, "pad-fire1 = 3\npad-fire2 = 2\n");
        CHECK(!strcmp(settings_value(&s, f1), "3") && !strcmp(settings_value(&s, f2), "2"));
    }

    /* values come from the file; absent keys take the game's defaults */
    settings_load(&s, "; mine\npresentation = Enhanced\nfullscreen = true\nscale = 4\n");
    CHECK(!strcmp(settings_value(&s, pres), "enhanced"));
    CHECK(!strcmp(settings_value(&s, fixes), "recommended"));
    CHECK(!strcmp(settings_value(&s, full), "yes"));
    CHECK(!strcmp(settings_value(&s, scale), "4"));
    CHECK(!s.open && !settings_key_event(&s, SKEY_DOWN));     /* closed: keys belong to the game */

    /* navigating and changing; choices wrap */
    settings_open(&s);
    CHECK(s.open && s.cursor == n + 14);
    CHECK(settings_key_event(&s, SKEY_ENTER) && s.page == SETTINGS_VIDEO && s.cursor == pres);
    CHECK(settings_key_event(&s, SKEY_RIGHT));                /* presentation: enhanced -> remastered -> original */
    CHECK(!strcmp(settings_value(&s, pres), "remastered"));
    CHECK(settings_key_event(&s, SKEY_RIGHT));
    CHECK(!strcmp(settings_value(&s, pres), "original"));
    CHECK(settings_key_event(&s, SKEY_LEFT));
    CHECK(!strcmp(settings_value(&s, pres), "remastered"));
    CHECK(settings_key_event(&s, SKEY_LEFT));
    CHECK(!strcmp(settings_value(&s, pres), "enhanced"));
    CHECK(!settings_needs_restart(&s));
    CHECK(settings_key_event(&s, SKEY_ESC) && s.page == SETTINGS_MAIN);
    CHECK(settings_key_event(&s, SKEY_UP) && s.cursor == n + 9);   /* wraps to the last row */
    const int main_rows = settings_menu_rows(&s, NULL, 0);
    CHECK(s.top == main_rows - 10);
    for (int k = 0; k < main_rows - 1; k++) settings_key_event(&s, SKEY_UP);
    CHECK(s.cursor == n + 14 && s.top == 0);                       /* and back to the top */
    settings_key_event(&s, SKEY_UP);
    CHECK(s.cursor == n + 9);
    /* Key remapping lives on the Keys tab: the Settings list no longer offers Remap a key, Clear key remaps or the controller-button rows */
    {
        int rows[48];
        const int listed = settings_menu_rows(&s, rows, 48);
        for (int k = 0; k < listed; k++) CHECK(rows[k] != n + 2 && rows[k] != n + 3 && rows[k] != n + 7 && rows[k] != n + 8);
    }
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
    /* Blue means different from the default, including values already saved.
     * Long fix descriptions must show their tail, not silently drop it. */
    {
        static uint32_t before[640 * 400], after[640 * 400];
        settings_load(&s, ""); settings_open(&s);
        settings_render(&s, before, 640, 400);
        int blue = 0;
        for (int i = 0; i < 640 * 400; ++i) blue += before[i] == 0xFF60B0FFu;
        CHECK(blue == 0);
        CHECK(settings_key_event(&s, SKEY_ENTER) && s.page == SETTINGS_VIDEO);
        settings_mark_saved(&s, "presentation", "remastered");
        settings_render(&s, after, 640, 400);
        for (int i = 0; i < 640 * 400; ++i) blue += after[i] == 0xFF60B0FFu;
        CHECK(blue > 0 && !settings_needs_restart(&s));
        settings_set_fix(&s, 0, "LONG", "This is a long fix description whose complete explanation must wrap within the panel even after the old two-line limit. TAILAAA", 1);
        s.picker = 1; s.pick_cursor = s.pick_top = 0;
        memset(before, 0, sizeof before); memset(after, 0, sizeof after);
        settings_render(&s, before, 640, 400);
        strcpy(strstr(s.pick_what[0], "TAILAAA"), "TAILBBB");
        settings_render(&s, after, 640, 400);
        CHECK(memcmp(before, after, sizeof before) != 0);
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 640; ++x)
                if (after[y * 640 + x]) CHECK(x >= 120 && x < 520 && y >= 0 && y < 400);
    }
    /* the Folders screen: typed or pasted text is kept per folder, Backspace and Esc work, and Save writes only what changed */
    {
        int rows;
        settings_defs(&rows);
        settings_load(&s, "; mine\ndata = C:/Old/Game\nmt32-roms = C:/Roms\n");
        CHECK(!strcmp(s.text_val[0], "C:/Old/Game") && !strcmp(s.text_val[1], "C:/Roms") && s.text_val[2][0] == 0);
        settings_open(&s);
        const int steps = settings_steps_to_row(&s, rows + 6);
        CHECK(steps >= 0);
        for (int i = 0; i < steps; i++) settings_key_event(&s, SKEY_DOWN);
        settings_key_event(&s, SKEY_ENTER);
        CHECK(s.text_picker && s.text_edit < 0);
        settings_text_input(&s, "ignored");                                          /* not editing yet */
        CHECK(!strcmp(s.text_val[0], "C:/Old/Game"));
        settings_key_event(&s, SKEY_ENTER);                                          /* edit the game folder */
        CHECK(s.text_edit == 0);
        settings_key_event(&s, SKEY_BACKSPACE); settings_key_event(&s, SKEY_BACKSPACE); settings_key_event(&s, SKEY_BACKSPACE); settings_key_event(&s, SKEY_BACKSPACE);
        settings_text_input(&s, "New\x01\xC3\xA9");                                  /* control and non-ASCII bytes are dropped */
        CHECK(!strcmp(s.text_val[0], "C:/Old/New"));
        settings_key_event(&s, SKEY_ESC);                                            /* undo */
        CHECK(s.text_edit < 0 && !strcmp(s.text_val[0], "C:/Old/Game") && !settings_needs_restart(&s));
        settings_key_event(&s, SKEY_ENTER);
        settings_key_event(&s, SKEY_BACKSPACE);
        settings_text_input(&s, "e2");
        settings_key_event(&s, SKEY_ENTER);                                          /* keep: C:/Old/Game -> C:/Old/Game2 */
        CHECK(!strcmp(s.text_val[0], "C:/Old/Game2") && settings_needs_restart(&s));
        settings_key_event(&s, SKEY_DOWN);                                           /* the ROM folder: cleared with Backspace */
        settings_key_event(&s, SKEY_BACKSPACE);
        CHECK(s.text_val[1][0] == 0);
        settings_key_event(&s, SKEY_ESC);                                            /* back to the menu */
        CHECK(!s.text_picker && s.open);
        char *out = settings_apply(&s, "; mine\ndata = C:/Old/Game\nmt32-roms = C:/Roms\n");
        CHECK(out);
        char v[300];
        CHECK(config_get(out, "data", v, sizeof v) && !strcmp(v, "C:/Old/Game2"));
        CHECK(!config_get(out, "mt32-roms", v, sizeof v));                           /* cleared: the key is gone */
        free(out);
        settings_key_event(&s, SKEY_ESC);                                            /* Esc in the menu drops the edits */
        CHECK(!s.open && !strcmp(s.text_val[0], "C:/Old/Game") && !strcmp(s.text_val[1], "C:/Roms"));
        CHECK(!strcmp(settings_text_key(0), "data") && !strcmp(settings_text_key(1), "mt32-roms") && !strcmp(settings_text_key(2), "mac-sounds"));
        /* it draws, in bounds */
        static uint32_t px[640 * 400];
        settings_open(&s);
        const int folder_steps = settings_steps_to_row(&s, rows + 6);
        CHECK(folder_steps >= 0);
        for (int i = 0; i < folder_steps; i++) settings_key_event(&s, SKEY_DOWN);
        settings_key_event(&s, SKEY_ENTER);
        settings_key_event(&s, SKEY_ENTER);
        settings_render(&s, px, 640, 400);
        int lit = 0;
        for (int i = 0; i < 640 * 400; i++) lit += px[i] != 0;
        CHECK(lit > 5000);
    }
    /* Risk-sensitive options and sound controls are reachable in their own
     * submenus, absent from the main list, and Back retains pending edits. */
    {
        const char *ini = "; keep\nengine = interp\nips = 9000000\nsim-hz = 8\ntiming = 386\nfixes = custom\nfix = D2\nremap = Q:W\npad-keys = 2:G\ndata = C:/Game\n";
        settings_load(&s, ini); settings_open(&s);
        int menu[48], count = settings_menu_rows(&s, menu, 48);
        const setting_def *defs = settings_defs(NULL);
        for (int i = 0; i < count; ++i)
            if (menu[i] < n) CHECK(settings_group(defs[menu[i]].key) == SETTINGS_MAIN);
        int steps = settings_steps_to_row(&s, n + 10);
        CHECK(steps >= 0);
        for (int i = 0; i < steps; ++i) settings_key_event(&s, SKEY_DOWN);
        settings_key_event(&s, SKEY_ENTER);
        CHECK(s.page == SETTINGS_ADVANCED && s.open);
        count = settings_menu_rows(&s, menu, 48);
        CHECK(count == 5);
        for (int i = 0; i < count - 1; ++i) CHECK(settings_group(defs[menu[i]].key) == SETTINGS_ADVANCED);
        settings_key_event(&s, SKEY_RIGHT);
        CHECK(settings_needs_restart(&s));
        settings_key_event(&s, SKEY_ESC);
        CHECK(s.page == SETTINGS_MAIN && s.open && settings_needs_restart(&s));
        steps = settings_steps_to_row(&s, n + 11);
        for (int i = 0; i < steps; ++i) settings_key_event(&s, SKEY_DOWN);
        settings_key_event(&s, SKEY_ENTER);
        CHECK(s.page == SETTINGS_AUDIO && s.open);
        count = settings_menu_rows(&s, menu, 48);
        CHECK(count == 8);
        for (int i = 0; i < count - 1; ++i) CHECK(settings_group(defs[menu[i]].key) == SETTINGS_AUDIO);
        settings_key_event(&s, SKEY_ESC);
        settings_reset_defaults(&s);
        for (int i = 0; i < n; ++i) CHECK(s.value[i] == defs[i].def);
        char *out = settings_apply(&s, ini), value[300];
        CHECK(out && strstr(out, "; keep"));
        CHECK(config_get(out, "engine", value, sizeof value) && !strcmp(value, "recomp"));
        CHECK(config_get(out, "data", value, sizeof value) && !strcmp(value, "C:/Game"));
        CHECK(!config_get(out, "remap", value, sizeof value) && !config_get(out, "pad-keys", value, sizeof value));
        CHECK(!config_get(out, "fix", value, sizeof value));
        free(out);
        settings_key_event(&s, SKEY_ESC);
        CHECK(!s.open && !s.reset_all);
        out = settings_apply(&s, ini);
        CHECK(out && !strcmp(out, ini));
        free(out);
    }
    {
        const int clock=find("ips");
        settings_load(&s,"ips = 12345678\n");settings_open(&s);
        CHECK(!strcmp(settings_value(&s,clock),"12345678") && !settings_needs_restart(&s));
        s.page=SETTINGS_ADVANCED;s.cursor=clock;settings_key_event(&s,SKEY_RIGHT);
        CHECK(strcmp(settings_value(&s,clock),"12345678") && settings_needs_restart(&s));
        settings_key_event(&s,SKEY_ESC);settings_key_event(&s,SKEY_ESC);
        CHECK(!strcmp(settings_value(&s,clock),"12345678"));
    }
    {
        settings_load(&s,"sound = ask\npresentation = original\n");settings_open(&s);
        settings_mark_saved(&s,"sound","roland");s.preserve_sound=1;s.value[pres]=2;
        char *args[]={"game.exe","--data","--sound","--presentation","original","--sound","adlib","--ips","12345678","--remap","Q:W",NULL};
        char **next=settings_restart_args(&s,args,"C:/folder with spaces/game.ini");CHECK(next);
        CHECK(!strcmp(next[1],"--data") && !strcmp(next[2],"--sound"));
        int presentations=0, sound_options=0, retained_ips=0, config=0;
        for(int i=1;next[i];) {
            const int arity=config_option_arity(next[i]);
            presentations+=!strcmp(next[i],"--presentation");
            if(!strcmp(next[i],"--sound")) {++sound_options;CHECK(!strcmp(next[i+1],"roland"));}
            if(!strcmp(next[i],"--ips")) retained_ips=!strcmp(next[i+1],"12345678");
            if(!strcmp(next[i],"--config")) config=!strcmp(next[i+1],"C:/folder with spaces/game.ini");
            CHECK(strcmp(next[i],"--remap"));i+=arity;
        }
        CHECK(!presentations && sound_options==1 && retained_ips && config);free(next);
        char *text=settings_apply(&s,"sound = ask\npresentation = original\n"),value[32];
        CHECK(text && config_get(text,"sound",value,sizeof value) && !strcmp(value,"roland"));free(text);
        settings_reset_defaults(&s);text=settings_apply(&s,"fullscreen=yes\nips=12345678\n");
        CHECK(text && config_get(text,"ips",value,sizeof value) && !strcmp(value,"60000000"));free(text);
        char *fixargs[]={"game.exe","--fix","none","--fixes","original",NULL};
        next=settings_restart_args(&s,fixargs,"game.ini");CHECK(next);
        for(int i=1;next[i];i++)CHECK(strcmp(next[i],"--fix") && strcmp(next[i],"--fixes"));free(next);
        settings_set_fix(&s,0,"QUIZ","Optional quiz",1);s.pick_default[0]=0;
        settings_reset_defaults(&s);CHECK(!s.pick_on[0]);
        settings_set_binding(&s,0,4,"A",4,"A");settings_set_binding(&s,1,41,"Escape",41,"Escape");
        s.key_picker=1;s.key_cursor=0;s.key_target=0;
        settings_key_event(&s,SKEY_ENTER);settings_key_event(&s,SKEY_END);settings_key_event(&s,SKEY_ENTER);
        CHECK(s.key_assign && s.remap_from==4 && s.remap_to==41 && !s.key_target);
        s.key_assign=0;settings_key_event(&s,SKEY_BACKSPACE);CHECK(s.key_assign && s.remap_from==s.remap_to);
        settings_key_event(&s,SKEY_ESC);CHECK(!s.key_picker && s.open);
    }
    puts("settings menu passes");
    return 0;
}
