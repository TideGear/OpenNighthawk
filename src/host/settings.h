/* settings.h - the in-game settings menu (docs/roadmap.md, Phase 3 "Re-release features").
 *
 * A menu over the options f117a.ini holds. It reads the file's current values, lets the player
 * change them, and writes them back with config_set; the host pauses the machine while it is open.
 * Most options take effect when the game next starts (the host offers to restart); an option marked
 * live is applied at once. A value given on the command line still wins over the file, which the menu
 * says. Nothing here touches the simulation. */
#ifndef F117R_SETTINGS_H
#define F117R_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *key;               /* the f117a.ini key */
    const char *label;
    const char *const *choices;    /* NULL-terminated; the file holds the choice's text */
    int def;                       /* index in choices when the file leaves the key out */
    int live;                      /* applied at once; the others wait for a restart */
} setting_def;

typedef enum { SKEY_UP, SKEY_DOWN, SKEY_LEFT, SKEY_RIGHT, SKEY_ENTER, SKEY_ESC } settings_key;

typedef struct {
    int open;
    int cursor;                    /* a setting, or the "Save", "Watch last flight" and "Close" rows after them */
    int top;                       /* the first row shown: the list scrolls when it is longer than the panel */
    int value[32];                 /* the choice index per setting */
    int saved[32];                 /* what the file holds */
    int at_open[32];               /* live settings as they were when the menu opened (the window's state) */
    int changed_live;              /* a live setting changed since the host last looked */
    int saved_now;                 /* the menu was closed with Save: the host writes the file */
    int remap_stage;               /* Remap a key was chosen: 1 waiting for the key to change, 2 for what it acts as */
    int remap_from;                /* the key chosen at stage 1 (an SDL scancode, set by the host) */
    int clear_remaps;              /* Clear key remaps was chosen: the host empties the list */
    char note[64];                 /* a line the host shows under the list (the last remap made) */
    int watch_now;                 /* "Watch last flight" was chosen: the host restarts into the newest recording */
    int want_restart;              /* Save was chosen with a restart-only setting changed */
} settings_state;

/* the table and its length */
const setting_def *settings_defs(int *count);

/* Read the values from the INI text (NULL for none) and close the menu. */
void settings_load(settings_state *s, const char *ini_text);

/* Open the menu. A live setting shows the program's actual state, not the file's: pass it with
 * settings_set_live after opening (key as in the table, index into its choices). */
void settings_open(settings_state *s);
void settings_set_live(settings_state *s, const char *key, int choice);

/* One key. Returns 1 when the menu used it (so the game does not see it). */
int settings_key_event(settings_state *s, settings_key k);

/* The INI text with every changed setting written (a malloc'd copy), or NULL on out of memory.
 * Settings equal to the file's are left alone. */
char *settings_apply(const settings_state *s, const char *ini_text);

/* Does any setting that needs a restart differ from what is saved? */
int settings_needs_restart(const settings_state *s);

/* The choice text of setting i. */
const char *settings_value(const settings_state *s, int i);

/* Draw the menu into an ARGB buffer (w*h, w >= 400, h >= 280), over whatever it holds: a dark panel
 * centred, 8x16 text. */
void settings_render(const settings_state *s, uint32_t *pixels, int w, int h);

#endif
