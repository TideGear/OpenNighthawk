/* settings.h - the in-game settings menu (docs/roadmap.md, Phase 3 "Re-release features").
 *
 * A menu over the options f117a.ini holds. It reads the file's current values, lets the player
 * change them, and writes them back with config_set; the host pauses the machine while it is open.
 * Most options take effect when the game next starts (the host offers to restart); an option marked
 * live is applied at once. Launch options win on initial startup; a settings
 * restart removes options conflicting with edited/saved values. Nothing here touches the simulation. */
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

#define SETTINGS_MAX_FIXES 48
#define SETTINGS_MAX_DEFS 48         /* the most settings the menu can hold: the per-setting arrays below are this long */

typedef enum { SKEY_UP, SKEY_DOWN, SKEY_LEFT, SKEY_RIGHT, SKEY_ENTER, SKEY_ESC, SKEY_BACKSPACE,
    SKEY_PAGEUP, SKEY_PAGEDOWN, SKEY_HOME, SKEY_END } settings_key;

#define SETTINGS_TEXTS 3             /* the folders the Folders screen edits: the game's, the MT-32 ROMs', the Mac recordings' */
#define SETTINGS_TEXT_MAX 260
enum { SETTINGS_MAIN, SETTINGS_AUDIO, SETTINGS_ADVANCED, SETTINGS_VIDEO };

typedef struct {
    int open;
    int cursor;                    /* a setting, or the "Save", "Watch last flight" and "Close" rows after them */
    int top;                       /* the first row shown: the list scrolls when it is longer than the panel */
    int page, parent_cursor, parent_top;
    int reset_all;                 /* pending defaults reset; only persisted by Save */
    int preserve_sound;            /* remember the active explicit sound choice across restart */
    int fixes_edited;              /* a custom set was saved; runtime still needs restarting */
    int key_picker, key_target, key_cursor, key_top, key_from, key_source_top, key_count;
    int key_assign, remap_to;
    struct { int physical, target; char name[32], target_name[32]; } keys[128];
    int value[SETTINGS_MAX_DEFS];               /* the choice index per setting */
    int saved[SETTINGS_MAX_DEFS];               /* what the file holds */
    char custom[SETTINGS_MAX_DEFS][64], saved_custom[SETTINGS_MAX_DEFS][64]; /* valid launch values outside the menu's presets */
    int at_open[SETTINGS_MAX_DEFS];             /* live settings as they were when the menu opened (the window's state) */
    int changed_live;              /* a live setting changed since the host last looked */
    int saved_now;                 /* the menu was closed with Save: the host writes the file */
    int remap_from;                /* the key chosen at stage 1 (an SDL scancode, set by the host) */
    int clear_remaps;              /* Clear key remaps was chosen: the host empties the list */
    int padkey_stage;              /* Controller button as key was chosen: 1 waiting for a controller button, 2 for the key it acts as */
    int padkey_button;             /* the button chosen at stage 1 (set by the host) */
    int clear_padkeys;             /* Clear controller keys was chosen: the host empties the list */
    char note[64];                 /* a line the host shows under the list (the last remap made) */
    int picker;                    /* the fix picker is up (Choose fixes...) */
    int pick_cursor, pick_top, pick_count;
    int picks_changed;             /* the picker closed with a change: the host writes fixes = custom and the list */
    unsigned char pick_on[SETTINGS_MAX_FIXES];
    unsigned char pick_default[SETTINGS_MAX_FIXES];
    char pick_id[SETTINGS_MAX_FIXES][8];
    char pick_what[SETTINGS_MAX_FIXES][160];
    int flight_count;              /* recorded flights listed for Watch a flight (newest first) */
    char flight_name[16][40];      /* the session folder names */
    char flight_what[16][64];      /* each one as the list shows it: the date and the log's size */
    int flight_picker;             /* the flight list is up */
    char watch_name[40];           /* the flight chosen; empty with watch_now means the newest */
    int watch_now;                 /* "Watch last flight" was chosen: the host restarts into the newest recording */
    int want_restart;              /* Save was chosen with a restart-only setting changed */
    int text_picker;               /* the Folders screen is up */
    int text_edit;                 /* the row being typed into, or -1 */
    char text_val[SETTINGS_TEXTS][SETTINGS_TEXT_MAX];    /* what the screen shows */
    char text_saved[SETTINGS_TEXTS][SETTINGS_TEXT_MAX];  /* what the file holds */
    char text_old[SETTINGS_TEXT_MAX];                    /* the row's value before this edit, for Esc */
} settings_state;

/* the table and its length */
const setting_def *settings_defs(int *count);
/* Plain-language help for a setting key, including resource folders. */
const char *settings_description(const char *key);
int settings_group(const char *key);
int settings_menu_rows(const settings_state *s, int *rows, int capacity);
int settings_steps_to_row(const settings_state *s, int row);
void settings_reset_defaults(settings_state *s);
void settings_set_binding(settings_state *s, int index, int physical, const char *name, int target, const char *target_name);
void settings_mark_folder_saved(settings_state *s, const char *key, const char *value);
/* New pointer array, borrowed strings; free the array after use. Saved choices
 * supersede conflicting launch options while unrelated arguments survive. */
char **settings_restart_args(const settings_state *s, char **original, const char *config_path);

/* Read the values from the INI text (NULL for none) and close the menu. */
void settings_load(settings_state *s, const char *ini_text);

/* Open the menu. A live setting shows the program's actual state, not the file's: pass it with
 * settings_set_live after opening (key as in the table, index into its choices). */
void settings_open(settings_state *s);

/* The host lists the fixes for the picker (index 0 up, in the order shown): id, what it does, whether it is on. */
void settings_set_fix(settings_state *s, int index, const char *id, const char *what, int on);

/* The host lists a recorded flight for Watch a flight (index 0 up, newest first). */
void settings_set_flight(settings_state *s, int index, const char *name, const char *what);

/* The host wrote a setting itself: show and remember it as saved (a value from that setting's choices). */
void settings_mark_saved(settings_state *s, const char *key, const char *value);
void settings_set_live(settings_state *s, const char *key, int choice);

/* Typed text (UTF-8, as SDL reports it, or a paste) while a folder is being edited; only printable ASCII is kept. */
void settings_text_input(settings_state *s, const char *utf8);

/* The label and the f117a.ini key of folder row i (0 to SETTINGS_TEXTS - 1). */
const char *settings_text_label(int i);
const char *settings_text_key(int i);

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
