/* settings.c - the in-game settings menu (see settings.h). */
#include "settings.h"

#include "config.h"
#include "font8x16.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const C_PRESENTATION[] = { "original", "enhanced", "remastered", NULL };
static const char *const C_FIXES[] = { "original", "recommended", "custom", NULL };
static const char *const C_SCALE[] = { "1", "2", "3", "4", "5", "6", NULL };
static const char *const C_YESNO[] = { "no", "yes", NULL };
static const char *const C_SPEAKER[] = { "realsound", "pwm", NULL };
static const char *const C_TIMING[] = { "dosbox", "386", NULL };
static const char *const C_SOUND[] = { "ask", "adlib", "roland", "speaker", NULL };
static const char *const C_SOURCE[] = { "default", "mac", "dos", NULL };      /* the hybrid sound policy: the Mac recordings where there are any, or only the DOS drivers */
static const char *const C_ROLAND[] = { "munt", "windows", "off", NULL };
static const char *const C_HZ[] = { "8", "15", NULL };
static const char *const C_OPL[] = { "dbopl", "nuked", NULL };
static const char *const C_WIDTH[] = { "auto", "1", "2", "3", "4", NULL };
static const char *const C_AGE[] = { "interp", "extrapolate", NULL };
static const char *const C_DEAD[] = { "0", "5", "10", "15", "20", "25", "30", NULL };
static const char *const C_BUTTON[] = { "0", "1", "2", "3", "4", "5", "6", "7", NULL };   /* SDL gamepad order: 0 south, 1 east, 2 west, 3 north */
static const char *const C_ENGINE[] = { "recomp", "interp", NULL };
static const char *const C_PSCALE[] = { "auto", "1", "2", "3", "4", "5", "6", "9", NULL };
static const char *const C_IPS[] = { "9000000", "20000000", "40000000", "60000000", "90000000", NULL };
static const char *const C_DRAW[] = { "original", "near", "far", "maximum", NULL };
static const char *const C_DOTS[] = { "original", "near", "far", "maximum", NULL };
static const char *const C_PICKORD[] = { "exact", "fast", NULL };
static const char *const C_VIEW[] = { "wide", "4:3", NULL };
static const char *const C_DETAIL[] = { "full", "original", NULL };
static const char *const C_FAR_LIGHT[] = { "original", "perspective", NULL };
static const char *const C_COLOURBLIND[] = { "off", "protanopia", "deuteranopia", "tritanopia", NULL };

/* The folders the Folders screen edits, typed or pasted. */
static const struct { const char *key, *label; } TEXTS[SETTINGS_TEXTS] = {
    { "data", "Game folder" }, { "mt32-roms", "MT-32 ROM folder" }, { "mac-sounds", "Mac recordings" },
    { "save", "Save folder" }, { "screenshots", "Screenshots folder" },
};
const char *settings_text_label(int i) { return i >= 0 && i < SETTINGS_TEXTS ? TEXTS[i].label : ""; }
const char *settings_text_key(int i) { return i >= 0 && i < SETTINGS_TEXTS ? TEXTS[i].key : ""; }

/* Fullscreen is live (Alt+Enter does the same); the rest are read when the game starts. */
static const setting_def DEFS[] = {
    { "presentation", "Presentation",     C_PRESENTATION, 0, 0 },
    { "fixes",        "Fixes",            C_FIXES,        1, 0 },
    { "scale",        "Window size",      C_SCALE,        2, 0 },
    { "fullscreen",   "Fullscreen",       C_YESNO,        0, 1 },
    { "speaker",      "Speaker speech",   C_SPEAKER,      0, 0 },
    { "timing",       "Machine speed",    C_TIMING,       0, 0 },
    { "ips",          "Machine clock /s", C_IPS,          3, 0 },     /* instructions a second: the game runs at 60 million, GOG's DOSBox at 9 */
    { "crt",          "CRT scanlines",    C_YESNO,        0, 0 },
    { "crt-mask",     "CRT phosphor mask", C_YESNO,       0, 0 },
    { "crt-glow",     "CRT glow",         C_YESNO,        0, 0 },
    { "colourblind",  "Colour-blind aid", C_COLOURBLIND,  0, 0 },
    { "subtitles",    "Radio subtitles",  C_YESNO,        0, 0 },
    { "sound",        "Sound driver",     C_SOUND,        0, 0 },
    { "music-source", "Music from",       C_SOURCE,       0, 0 },
    { "sound-source", "Effects from",     C_SOURCE,       0, 0 },
    { "roland",       "Roland output",    C_ROLAND,       2, 0 },
    { "adlib-effects", "AdLib effects",   C_YESNO,        1, 0 },     /* on unless the file says no: the default with Roland */
    { "sim-hz",       "Game steps a sec", C_HZ,           1, 0 },
    { "opl",          "AdLib emulation",  C_OPL,          0, 0 },
    { "no-aspect",    "Ignore 4:3 shape", C_YESNO,        0, 0 },
    { "draw-distance", "Draw distance",   C_DRAW,         0, 1 },     /* Remastered: how far scenery is drawn; applied at once */
    { "dot-distance", "Terrain dots",     C_DOTS,         0, 1 },
    { "model-detail", "Model detail",     C_DETAIL,       0, 1 },     /* Remastered: full (every model at its finest version) or original (by distance); applied at once */
    { "far-light-size", "Far light size", C_FAR_LIGHT,    0, 1 },     /* Remastered: original or perspective (R35: lights and lines beyond the game's reach shrink with distance); applied at once */
    { "remaster-view", "Remastered view", C_VIEW,         0, 1 },     /* Remastered: wide (fills a wide window) or 4:3 (the original's framing); applied at once */
    { "pick-order", "Far object order", C_PICKORD,    0, 1 },     /* Remastered: exact (the game's own order) or fast (cheaper by about 0.65 ms a frame); applied at once */     /* Remastered: how far the small dark ground dots are drawn; applied at once */
    { "present-scale", "Picture detail",  C_PSCALE,       0, 0 },     /* fine pixels per original pixel; auto fits the display */
    { "present-line-width", "Line width",  C_WIDTH,        0, 0 },
    { "present-age",  "Picture delay",    C_AGE,          0, 0 },
    { "pad-deadzone", "Stick dead zone %", C_DEAD,        0, 0 },
    { "pad-fire1",    "Fire 1 button",    C_BUTTON,       0, 0 },     /* the cannon (the game port's first button) */
    { "pad-fire2",    "Fire 2 button",    C_BUTTON,       1, 0 },     /* the weapon */
    { "pad-invert-x", "Invert stick X",   C_YESNO,        0, 0 },
    { "pad-invert-y", "Invert stick Y",   C_YESNO,        0, 0 },
    { "engine",       "CPU engine",       C_ENGINE,       0, 0 },
};
#define NDEFS ((int)(sizeof DEFS / sizeof DEFS[0]))
#define SETTINGS_VISIBLE 10                           /* leave room for the selected setting's description */

static int changed(const settings_state *s,int i)
{return (s->fixes_edited && !strcmp(DEFS[i].key,"fixes")) || s->value[i]!=s->saved[i] || strcmp(s->custom[i],s->saved_custom[i]);}

const char *settings_description(const char *key)
{
    static const struct { const char *key, *text; } help[] = {
        { "presentation", "Original preserves the DOS picture. Enhanced adds finer drawing. Remastered rebuilds the 3D view." },
        { "fixes", "Original disables repairs. Recommended enables them. Custom uses your choices in Choose fixes." },
        { "scale", "Sets the starting window size. Picture detail controls how finely the world is drawn." },
        { "fullscreen", "Fills the display with the game. Alt+Enter also switches this immediately." },
        { "speaker", "RealSound reconstructs digitized speech. PWM reproduces the original speaker pulses." },
        { "timing", "Selects the emulated CPU timing model. Changing this can affect game behavior and compatibility." },
        { "ips", "Sets the emulated instruction budget per second. Higher values use more CPU and can affect behavior." },
        { "crt", "Adds dark scanlines to the displayed picture, like a CRT monitor." },
        { "crt-mask", "Adds a colored phosphor pattern to simulate a CRT screen." },
        { "crt-glow", "Adds a soft glow around bright parts of the displayed picture." },
        { "colourblind", "Changes HUD colors for the selected color-vision aid. Off keeps the original colors." },
        { "subtitles", "Shows radio calls as text so they can be read as well as heard." },
        { "sound", "Chooses the original DOS sound driver. Ask shows the original SETUP questions at startup." },
        { "music-source", "Default prefers your Mac recordings when available. Mac uses those recordings; DOS uses the selected DOS driver." },
        { "sound-source", "Selects the source of effects and speech. Mac recordings require your own recording folder." },
        { "roland", "Munt plays MT-32 music using your ROMs. Windows sends MIDI to a device. Off disables Roland output." },
        { "adlib-effects", "Plays AdLib effects beside Roland music, using a separate copy of the original AdLib driver." },
        { "sim-hz", "Chooses simulation updates per second. This changes the game's update cadence, not the monitor refresh rate." },
        { "opl", "Chooses the AdLib synthesizer. DBOPL is faster; Nuked favors chip emulation accuracy." },
        { "no-aspect", "Stretches the original pixels without correcting their intended 4:3 display shape." },
        { "draw-distance", "Remastered only: how far scenery is drawn. The game stops drawing each object at a fixed depth, so buildings appear suddenly; each step here draws far objects one scale coarser, four times farther: Near one step (the default, almost free), Far two, Maximum the whole map. Original keeps the game's own reach. Farther costs picture rate." },
        { "pick-order", "Remastered only: the order far objects are drawn in. Exact keeps the game's own order for objects the same distance away. Fast sorts them with a cheaper method that saves about 0.65 ms a frame at Maximum draw distance and lets objects tied on distance swap places, which changes a few edge pixels in some frames." },
        { "model-detail", "Remastered only: Full draws every building and object in its most detailed version at any distance, so their lights no longer appear suddenly as you fly closer (costs about 1 ms a frame at 4K). Original picks the version by distance, as the game does." },
        { "far-light-size", "Remastered only: how big far lights and lines look. Original draws them one game pixel wide at any distance, so far lights become blocks. Perspective shrinks those beyond the game's own reach with distance, down to one screen pixel. Has no effect at Draw distance Original." },
        { "remaster-view", "Remastered only: Wide fills a wide window with a wider view of the world. 4:3 keeps the original's framing, with black bars at the sides of a wide window; the picture is still redrawn without the original's road and coastline jitter." },
        { "dot-distance", "Remastered only: how far the small dark dots that add detail to plain ground are drawn. Original keeps the game's own reach; each step reaches one scale coarser, up to Maximum. Far lights and buildings follow Draw distance instead." },
        { "present-scale", "Sets render resolution as a multiplier on the original 320 x 200 picture: 3 draws three pixels per original pixel on each axis (960 x 600), 9 draws 2880 x 1800. The size is shown beside the number. Auto fits the display. Higher values cost more CPU time." },
        { "present-line-width", "Changes fine-line thickness. Auto scales the width with resolution." },
        { "present-age", "Interpolate gives smooth motion with some delay. Extrapolate predicts ahead and can overshoot sudden changes." },
        { "pad-deadzone", "Ignores small stick movements near the center to prevent unwanted drift." },
        { "pad-fire1", "Chooses the controller button used for the game's first trigger, normally the cannon." },
        { "pad-fire2", "Chooses the controller button used for the game's second trigger, normally the selected weapon." },
        { "pad-invert-x", "Reverses the controller stick's left and right directions." },
        { "pad-invert-y", "Reverses the controller stick's forward and backward directions." },
        { "engine", "Recomp runs the game faster. Interp is a slower alternative for diagnosing compatibility problems." },
        { "data", "Folder containing your original DOS game files. Clear to use the normal game-folder search." },
        { "mt32-roms", "Folder containing your MT-32 ROM files for Munt's Roland music output." },
        { "mac-sounds", "Folder containing recordings extracted from your own Macintosh copy of the game." },
        { "save", "Folder for your pilots, settings history and recorded flights. Clear to use the default under your user profile." },
        { "screenshots", "Folder where Tab saves pictures, as PNG files named OpenNighthawk and the date and time. Clear for the Pictures folder, in OpenNighthawk." },
    };
    for (unsigned i = 0; i < sizeof help / sizeof help[0]; ++i)
        if (!strcmp(help[i].key, key)) return help[i].text;
    return "";
}

/* Never discard the tail of a description. A long word is split only when
 * it cannot fit on an otherwise empty line. */
static int wrap_piece(const char *text, int columns)
{
    int n = 0;
    while (text[n] && n < columns) ++n;
    if (text[n]) {
        int word = n;
        while (word > 0 && text[word] != ' ') --word;
        if (word) n = word;
    }
    return n;
}

static int fix_height(const settings_state *s, int index)
{
    const char *text = s->pick_what[index];
    int lines = 0;
    do {
        text += wrap_piece(text, 34);
        while (*text == ' ') ++text;
        ++lines;
    } while (*text);
    return lines * 16 + 8;
}

int settings_group(const char *key)
{
    static const char *const audio[] = { "sound", "music-source", "sound-source", "roland", "adlib-effects", "speaker", "opl", NULL };
    static const char *const video[] = { "presentation", "scale", "fullscreen", "crt", "crt-mask", "crt-glow", "colourblind", "subtitles", "no-aspect", "draw-distance", "dot-distance", "model-detail", "far-light-size", "pick-order", "remaster-view", "present-scale", "present-line-width", "present-age", NULL };
    for (int i = 0; video[i]; ++i) if (!strcmp(key, video[i])) return SETTINGS_VIDEO;
    static const char *const advanced[] = { "timing", "ips", "sim-hz", "engine", NULL };
    for (int i = 0; audio[i]; ++i) if (!strcmp(key, audio[i])) return SETTINGS_AUDIO;
    for (int i = 0; advanced[i]; ++i) if (!strcmp(key, advanced[i])) return SETTINGS_ADVANCED;
    return SETTINGS_MAIN;
}

int settings_menu_rows(const settings_state *s, int *rows, int capacity)
{
    int count = 0;
#define ROW(value_) do { if (rows && count < capacity) rows[count] = (value_); ++count; } while (0)
    if (s->page == SETTINGS_MAIN) ROW(NDEFS + 14);
    for (int i = 0; i < NDEFS; ++i)
        if (settings_group(DEFS[i].key) == s->page) ROW(i);
    if (s->page == SETTINGS_MAIN) {
        ROW(NDEFS + 11); ROW(NDEFS + 10);
        for (int i = 0; i < 9; ++i) if (i != 2 && i != 3 && i != 7 && i != 8) ROW(NDEFS + i);   /* key and controller-button assignment live on the Keys tab */
        ROW(NDEFS + 12); ROW(NDEFS + 9);
    } else ROW(NDEFS + 13);
#undef ROW
    return count;
}

int settings_steps_to_row(const settings_state *s, int row)
{
    int rows[48], current = 0, target = -1;
    const int count = settings_menu_rows(s, rows, 48);
    for (int i = 0; i < count; ++i) {
        if (rows[i] == s->cursor) current = i;
        if (rows[i] == row) target = i;
    }
    return target < 0 ? -1 : (target + count - current) % count;
}

void settings_reset_defaults(settings_state *s)
{
    for (int i = 0; i < NDEFS; ++i) {
        if (DEFS[i].live && s->value[i] != DEFS[i].def) s->changed_live = 1;
        s->value[i] = DEFS[i].def;
        s->custom[i][0]=0;
    }
    s->reset_all = 1;
    for (int i = 0; i < s->pick_count; ++i) s->pick_on[i] = s->pick_default[i];
    snprintf(s->note, sizeof s->note, "Defaults selected. Save to keep them.");
}

void settings_set_binding(settings_state *s, int index, int physical, const char *name, int target, const char *target_name)
{
    if (index < 0 || index >= 128) return;
    s->keys[index].physical=physical; s->keys[index].target=target;
    snprintf(s->keys[index].name,sizeof s->keys[index].name,"%s",name);
    snprintf(s->keys[index].target_name,sizeof s->keys[index].target_name,"%s",target_name);
    if (index >= s->key_count) s->key_count=index+1;
}

void settings_mark_folder_saved(settings_state *s,const char *key,const char *value)
{
    for(int i=0;i<SETTINGS_TEXTS;i++)if(!strcmp(key,TEXTS[i].key)) {
        snprintf(s->text_val[i],SETTINGS_TEXT_MAX,"%s",value);
        snprintf(s->text_saved[i],SETTINGS_TEXT_MAX,"%s",value);
    }
}

char **settings_restart_args(const settings_state *s, char **original, const char *path)
{
    int count=0, n=0, roland_changed=0, sound=-1;
    while (original[count]) ++count;
    char **out=calloc((size_t)count+9,sizeof *out);
    if (!out) return NULL;
    out[n++]=original[0];
    for (int k=0;k<NDEFS;k++) {
        if (!strcmp(DEFS[k].key,"roland")) roland_changed=s->reset_all || changed(s,k);
        if (!strcmp(DEFS[k].key,"sound")) sound=k;
    }
    for (int i=1;i<count;) {
        const char *a=original[i];
        const int arity=config_option_arity(a), available=arity==2 && i+1<count ? 2 : 1;
        int remove=!strcmp(a,"--config") || !strcmp(a,"--no-config") ||
            !strcmp(a,"--remap") || !strcmp(a,"--pad-keys");
        for (int k=0;k<NDEFS;k++) if (s->reset_all || changed(s,k) ||
                                             (s->preserve_sound && k==sound)) {
            if (!strncmp(a,"--",2) && !strcmp(a+2,DEFS[k].key)) remove=1;
            if (!strcmp(DEFS[k].key,"adlib-effects") && !strcmp(a,"--no-adlib-effects")) remove=1;
            if (!strcmp(DEFS[k].key,"presentation") && !strcmp(a,"--present")) remove=1;
            if (!strcmp(DEFS[k].key,"fixes") && !strcmp(a,"--fix")) remove=1;
        }
        for (int k=0;k<SETTINGS_TEXTS;k++)
            if (strcmp(s->text_val[k],s->text_saved[k]) && !strncmp(a,"--",2) && !strcmp(a+2,TEXTS[k].key)) remove=1;
        if (roland_changed && (!strcmp(a,"--midi") || !strcmp(a,"--mt32-control") || !strcmp(a,"--mt32-pcm") || !strcmp(a,"--mt32-seed"))) remove=1;
        if (!remove) for (int j=0;j<available;j++) out[n++]=original[i+j];
        i+=available;
    }
    out[n++]="--config";out[n++]=(char *)path;
    /* A retained ROM-folder launch option also selects Roland output. Put
     * the saved output choice last so that folder cannot undo Off/Windows. */
    if (roland_changed) for (int k=0;k<NDEFS;k++) if (!strcmp(DEFS[k].key,"roland")) {
        out[n++]="--roland";out[n++]=(char *)settings_value(s,k);
    }
    if (s->preserve_sound && sound>=0) {out[n++]="--sound";out[n++]=(char *)settings_value(s,sound);}
    return out;
}

const setting_def *settings_defs(int *count)
{
    if (count) *count = NDEFS;
    return DEFS;
}

static int index_of(const setting_def *d, const char *text)
{
    for (int i = 0; d->choices[i]; i++)
        if (!strcmp(d->choices[i], text) || (i == 0 && d->choices == C_WIDTH && !strcmp(text, "0"))) return i;
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
        char v[64]="";
        int at = -1;
        if (config_get(ini_text, DEFS[i].key, v, sizeof v)) {
            for (char *p = v; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
            if (DEFS[i].choices == C_YESNO && (!strcmp(v, "true") || !strcmp(v, "on") || !strcmp(v, "1"))) snprintf(v, sizeof v, "yes");
            if (DEFS[i].choices == C_YESNO && (!strcmp(v, "false") || !strcmp(v, "off") || !strcmp(v, "0"))) snprintf(v, sizeof v, "no");
            at = index_of(&DEFS[i], v);
        }
        s->value[i] = s->saved[i] = at >= 0 ? at : DEFS[i].def;
        if(at<0 && v[0]) {snprintf(s->custom[i],64,"%s",v);snprintf(s->saved_custom[i],64,"%s",v);}
    }
    s->text_edit = -1;
    for (int i = 0; i < SETTINGS_TEXTS; i++) {
        char v[SETTINGS_TEXT_MAX] = "";
        if (!ini_text || !config_get(ini_text, TEXTS[i].key, v, sizeof v)) v[0] = 0;
        memcpy(s->text_val[i], v, sizeof v);
        memcpy(s->text_saved[i], v, sizeof v);
    }
}

void settings_open(settings_state *s)
{
    s->open = 1;
    s->cursor = NDEFS + 14;
    s->top = 0;
    s->page = SETTINGS_MAIN;
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
    s->pick_default[index] = s->pick_on[index];
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
            if (at >= 0) {s->value[i] = s->saved[i] = at;s->custom[i][0]=s->saved_custom[i][0]=0;}
            else {snprintf(s->custom[i],64,"%s",value);snprintf(s->saved_custom[i],64,"%s",value);}
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
        memcpy(s->custom[i],s->saved_custom[i],sizeof s->custom[i]);
    }
    for (int i = 0; i < SETTINGS_TEXTS; i++) memcpy(s->text_val[i], s->text_saved[i], SETTINGS_TEXT_MAX);
    s->text_picker = 0;
    s->text_edit = -1;
    s->open = 0;
    s->reset_all = 0;
}

int settings_needs_restart(const settings_state *s)
{
    if (s->reset_all) return 1;
    for (int i = 0; i < NDEFS; i++)
        if (!DEFS[i].live && changed(s,i)) return 1;
    for (int i = 0; i < SETTINGS_TEXTS; i++)
        if (strcmp(s->text_val[i], s->text_saved[i])) return 1;
    return 0;
}

const char *settings_value(const settings_state *s, int i)
{
    return s->custom[i][0] ? s->custom[i] : DEFS[i].choices[s->value[i]];
}

void settings_text_input(settings_state *s, const char *utf8)
{
    if (!s->open || !s->text_picker || s->text_edit < 0 || !utf8) return;
    char *v = s->text_val[s->text_edit];
    size_t n = strlen(v);
    for (; *utf8 && n + 1 < SETTINGS_TEXT_MAX; utf8++)
        if ((unsigned char)*utf8 >= 32 && (unsigned char)*utf8 < 127) v[n++] = *utf8;
    v[n] = 0;
}

int settings_key_event(settings_state *s, settings_key k)
{
    if (!s->open) return 0;
    if (s->key_picker) {
        const int count=s->key_count;
        if (!count) { if(k==SKEY_ESC)s->key_picker=0; return 1; }
        if (k==SKEY_UP) s->key_cursor=(s->key_cursor+count-1)%count;
        else if (k==SKEY_DOWN) s->key_cursor=(s->key_cursor+1)%count;
        else if (k==SKEY_PAGEUP) s->key_cursor=s->key_cursor<10 ? 0 : s->key_cursor-10;
        else if (k==SKEY_PAGEDOWN) s->key_cursor=s->key_cursor+10<count ? s->key_cursor+10 : count-1;
        else if (k==SKEY_HOME) s->key_cursor=0;
        else if (k==SKEY_END) s->key_cursor=count-1;
        else if (k==SKEY_ENTER) {
            if (!s->key_target) {
                s->key_from=s->key_cursor; s->key_source_top=s->key_top; s->key_target=1;
                for(int i=0;i<count;i++) if(s->keys[i].physical==s->keys[s->key_from].target)s->key_cursor=i;
            } else {
                s->remap_from=s->keys[s->key_from].physical; s->remap_to=s->keys[s->key_cursor].physical; s->key_assign=1;
                s->key_target=0; s->key_cursor=s->key_from; s->key_top=s->key_source_top;
            }
        } else if(k==SKEY_BACKSPACE && !s->key_target) {
            s->remap_from=s->remap_to=s->keys[s->key_cursor].physical; s->key_assign=1;
        } else if(k==SKEY_ESC) {
            if(s->key_target) {s->key_target=0;s->key_cursor=s->key_from;s->key_top=s->key_source_top;}
            else s->key_picker=0;
        }
        if(s->key_cursor<s->key_top)s->key_top=s->key_cursor;
        if(s->key_cursor>=s->key_top+10)s->key_top=s->key_cursor-9;
        return 1;
    }
    if (s->text_picker) {                          /* the Folders screen has the keys */
        if (s->text_edit >= 0) {                   /* typing: Enter keeps it, Esc puts the old text back */
            char *v = s->text_val[s->text_edit];
            if (k == SKEY_BACKSPACE) { size_t n = strlen(v); if (n) v[n - 1] = 0; }
            else if (k == SKEY_ENTER) s->text_edit = -1;
            else if (k == SKEY_ESC) { memcpy(v, s->text_old, SETTINGS_TEXT_MAX); s->text_edit = -1; }
            return 1;
        }
        if (k == SKEY_UP) s->pick_cursor = (s->pick_cursor + SETTINGS_TEXTS - 1) % SETTINGS_TEXTS;
        else if (k == SKEY_DOWN) s->pick_cursor = (s->pick_cursor + 1) % SETTINGS_TEXTS;
        else if (k == SKEY_ENTER || k == SKEY_RIGHT) { s->text_edit = s->pick_cursor; memcpy(s->text_old, s->text_val[s->text_edit], SETTINGS_TEXT_MAX); }
        else if (k == SKEY_BACKSPACE) s->text_val[s->pick_cursor][0] = 0;       /* clears the folder: the game's default again */
        else if (k == SKEY_ESC) s->text_picker = 0;
        return 1;
    }
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
        for (;;) {
            int height = 0;
            for (int i = s->pick_top; i <= s->pick_cursor; ++i) height += fix_height(s, i);
            if (height <= SETTINGS_VISIBLE * 20 - 8 || s->pick_top == s->pick_cursor) break;
            ++s->pick_top;
        }
        return 1;
    }
    int rows[48], at = 0;
    const int count = settings_menu_rows(s, rows, 48);
    for (int i = 0; i < count; ++i) if (rows[i] == s->cursor) at = i;
    switch (k) {
    case SKEY_UP: at = (at + count - 1) % count; s->cursor = rows[at]; break;
    case SKEY_DOWN: at = (at + 1) % count; s->cursor = rows[at]; break;
    case SKEY_LEFT: case SKEY_RIGHT:
        if (s->cursor < NDEFS) {
            const int n = count_of(&DEFS[s->cursor]);
            if(s->custom[s->cursor][0]) {s->custom[s->cursor][0]=0;s->value[s->cursor]=k==SKEY_RIGHT ? 0 : n-1;}
            else s->value[s->cursor] = (s->value[s->cursor] + (k == SKEY_RIGHT ? 1 : n - 1)) % n;
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
            s->key_picker = 1; s->key_target = s->key_cursor = s->key_top = 0;
        } else if (s->cursor == NDEFS + 3) {       /* Clear key remaps: the host empties the list at once */
            s->clear_remaps = 1;
        } else if (s->cursor == NDEFS + 4) {       /* Choose fixes: the picker, if the host listed the fixes */
            if (s->pick_count) { s->picker = 1; s->pick_cursor = 0; s->pick_top = 0; s->picks_changed = 0; }
        } else if (s->cursor == NDEFS + 5) {       /* Watch a flight: the list of recordings, if there are any */
            if (s->flight_count) { s->flight_picker = 1; s->pick_cursor = 0; s->pick_top = 0; }
        } else if (s->cursor == NDEFS + 6) {       /* Folders: the typed entries */
            s->text_picker = 1; s->text_edit = -1; s->pick_cursor = 0;
        } else if (s->cursor == NDEFS + 7) {       /* Controller button as key: the host takes a button, then a key */
            s->padkey_stage = 1;
        } else if (s->cursor == NDEFS + 8) {       /* Clear controller keys: the host empties the list at once */
            s->clear_padkeys = 1;
        } else if (s->cursor == NDEFS + 10 || s->cursor == NDEFS + 11 || s->cursor == NDEFS + 14) {
            s->parent_cursor = s->cursor; s->parent_top = s->top;
            s->page = s->cursor == NDEFS + 10 ? SETTINGS_ADVANCED : s->cursor == NDEFS + 11 ? SETTINGS_AUDIO : SETTINGS_VIDEO;
            settings_menu_rows(s, rows, 48); s->cursor = rows[0]; s->top = 0;
            return 1;
        } else if (s->cursor == NDEFS + 12) settings_reset_defaults(s);
        else if (s->cursor == NDEFS + 13) {
            s->page = SETTINGS_MAIN; s->cursor = s->parent_cursor; s->top = s->parent_top;
            return 1;
        } else revert(s);                          /* Close, without saving */
        break;
    case SKEY_ESC:
        if (s->page != SETTINGS_MAIN) {
            s->page = SETTINGS_MAIN; s->cursor = s->parent_cursor; s->top = s->parent_top;
            return 1;
        }
        revert(s);
        break;
    case SKEY_BACKSPACE:
    case SKEY_PAGEUP: case SKEY_PAGEDOWN: case SKEY_HOME: case SKEY_END:
        break;
    }
    if (at < s->top) s->top = at;
    if (at >= s->top + SETTINGS_VISIBLE) s->top = at - SETTINGS_VISIBLE + 1;
    return 1;
}

char *settings_apply(const settings_state *s, const char *ini_text)
{
    char *text = (char *)malloc(strlen(ini_text ? ini_text : "") + 1);
    if (!text) return NULL;
    memcpy(text, ini_text ? ini_text : "", strlen(ini_text ? ini_text : "") + 1);
    if (s->reset_all) {
        const char *keys[] = { "fix", "remap", "pad-keys", NULL };
        for (int i = 0; keys[i]; ++i) {
            char *next = config_set(text, keys[i], NULL);
            free(text); if (!next) return NULL; text = next;
        }
    }
    for (int i = 0; i < NDEFS; i++) {
        if (!s->reset_all && !changed(s,i) && !(s->preserve_sound && !strcmp(DEFS[i].key,"sound"))) continue;
        char *next = config_set(text, DEFS[i].key, settings_value(s,i));
        free(text);
        if (!next) return NULL;
        text = next;
    }
    for (int i = 0; i < SETTINGS_TEXTS; i++) {
        if (!strcmp(s->text_val[i], s->text_saved[i])) continue;
        char *next = config_set(text, TEXTS[i].key, s->text_val[i][0] ? s->text_val[i] : NULL);
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

static void put_wrapped(uint32_t *px, int w, int h, int x, int y, const char *text, int columns, int lines, uint32_t color)
{
    for (int line = 0; line < lines && *text; ++line) {
        char buf[64];
        const int n = wrap_piece(text, columns);
        memcpy(buf, text, (size_t)n); buf[n] = 0;
        put_text(px, w, h, x, y + line * 16, buf, color);
        text += n;
        while (*text == ' ') ++text;
    }
}

static const char *action_help(int action)
{
    static const char *const help[] = {
        "Writes your selected settings. A restart is offered when needed; fullscreen changes immediately.",
        "Restarts into the newest recorded flight using a private copy of its starting saves.",
        "Lists keyboard bindings. Choose a physical key to change what it sends to the game.",
        "Restores every keyboard key to its original binding.",
        "Shows the switchable game repairs. Read each description before choosing a custom set.",
        "Lists recorded flights so you can choose one to watch from its start.",
        "Sets the locations of your original game files, MT-32 ROMs and Macintosh recordings.",
        "Choose a controller button, then the keyboard key that button should press in the game.",
        "Removes controller-button keyboard assignments. The normal fire-button settings are kept.",
        "Closes the menu and discards pending option changes.",
        "CPU timing and simulation controls. Changes here can affect game behavior and compatibility.",
        "Groups the DOS driver, music and effects sources, Roland output and sound emulation options.",
        "Restores defaults and key mappings; Sound becomes Ask. Game and sound folders stay. Save applies; Close cancels.",
        "Returns to the main settings list, keeping your pending selections.",
        "Presentation, draw distance, picture detail and display options.",
    };
    return action >= 0 && action < (int)(sizeof help / sizeof help[0]) ? help[action] : "";
}

static int s_auto_scale;     /* what Picture detail "auto" resolves to on this display, for the menu's label (0: unknown) */
void settings_set_auto_scale(int n) { s_auto_scale = n; }

void settings_render(const settings_state *s, uint32_t *px, int w, int h)
{
    int rows[48];
    const int total = settings_menu_rows(s, rows, 48), shown = total < SETTINGS_VISIBLE ? total : SETTINGS_VISIBLE;
    const int pw = 400, ph = 24 + shown * 20 + 104;
    const int x0 = (w - pw) / 2, y0 = (h - ph) / 2;
    for (int y = y0; y < y0 + ph; y++)
        for (int x = x0; x < x0 + pw; x++) {
            const int edge = x < x0 + 2 || x >= x0 + pw - 2 || y < y0 + 2 || y >= y0 + ph - 2;
            px[(size_t)y * (size_t)w + (size_t)x] = edge ? 0xFFA0A0A0u : 0xFF101820u;
        }
    if (s->key_picker) {
        put_text(px,w,h,x0+16,y0+10,s->key_target ? "CHOOSE OUTPUT KEY" : "KEY BINDINGS",0xFFFFFFFFu);
        for(int i=s->key_top;i<s->key_count && i<s->key_top+10;i++) {
            const int y=y0+34+(i-s->key_top)*20;
            const uint32_t selected=i==s->key_cursor ? 0xFFFFE060u : 0xFFD0D0D0u;
            if(i==s->key_cursor)put_text(px,w,h,x0+12,y,">",selected);
            put_text(px,w,h,x0+30,y,s->keys[i].name,selected);
            if(!s->key_target)put_text(px,w,h,x0+218,y,s->keys[i].target_name,
                s->keys[i].physical!=s->keys[i].target ? 0xFF60B0FFu : selected);
        }
        char help[160];
        if(s->key_target)snprintf(help,sizeof help,"Choose what %s sends to the game. Enter assigns; Escape cancels.",s->keys[s->key_from].name);
        else snprintf(help,sizeof help,"Physical keys on the left, game keys on the right. Blue means changed from default. Host shortcuts use physical keys.");
        put_wrapped(px,w,h,x0+16,y0+ph-86,help,46,3,0xFFB0B0B0u);
        put_text(px,w,h,x0+16,y0+ph-38,s->key_target ? "Enter: assign   Esc: cancel" : "Enter: change  Backspace: default  Esc: back",0xFF909090u);
        put_text(px,w,h,x0+16,y0+ph-22,"PgUp/PgDn: page   Home/End: first/last",0xFF909090u);
        return;
    }
    if (s->text_picker) {                          /* the Folders screen */
        put_text(px, w, h, x0 + 16, y0 + 10, "FOLDERS", 0xFFFFFFFFu);
        for (int i = 0; i < SETTINGS_TEXTS; i++) {
            const int y = y0 + 34 + i * 40, on = i == s->pick_cursor, editing = s->text_edit == i;
            const uint32_t color = on ? 0xFFFFE060u : 0xFFD0D0D0u;
            if (on) put_text(px, w, h, x0 + 12, y, ">", color);
            put_text(px, w, h, x0 + 30, y, TEXTS[i].label, color);
            if (strcmp(s->text_val[i], s->text_saved[i])) put_text(px, w, h, x0 + 370, y, "*", 0xFF60E060u);
            char line[48];
            const char *v = s->text_val[i];
            const size_t n = strlen(v);
            if (n > 40) snprintf(line, sizeof line, "...%s%s", v + n - 37, editing ? "_" : "");
            else snprintf(line, sizeof line, "%s%s", v[0] ? v : "(the game's default)", editing ? "_" : "");
            put_text(px, w, h, x0 + 46, y + 18, line, editing ? 0xFFFFFFFFu : s->text_val[i][0] ? 0xFF60B0FFu : 0xFFA0A0A0u);
        }
        put_wrapped(px, w, h, x0 + 16, y0 + ph - 86, settings_description(TEXTS[s->pick_cursor].key), 46, 3, 0xFFB0B0B0u);
        put_text(px, w, h, x0 + 16, y0 + ph - 38, s->text_edit >= 0 ? "Type or paste (Ctrl+V). Enter keeps, Esc undoes." : "Enter: edit   Backspace: clear   Esc: back", 0xFF909090u);
        put_text(px, w, h, x0 + 16, y0 + ph - 22, "(applies after restart)", 0xFF909090u);
        return;
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
        put_text(px, w, h, x0 + 16, y0 + 10, "CHOOSE FIXES (custom set)", 0xFFFFFFFFu);
        int y = y0 + 34;
        for (int i = s->pick_top; i < s->pick_count; i++) {
            const int height = fix_height(s, i), on = i == s->pick_cursor;
            if (y + height > y0 + ph - 38) break;
            char line[24];
            snprintf(line, sizeof line, "[%c] %s", s->pick_on[i] ? 'x' : ' ', s->pick_id[i]);
            if (on) put_text(px, w, h, x0 + 12, y, ">", 0xFFFFE060u);
            put_text(px, w, h, x0 + 30, y, line, on ? 0xFFFFE060u : 0xFFD0D0D0u);
            put_wrapped(px, w, h, x0 + 110, y, s->pick_what[i], 34, height / 16, on ? 0xFFFFE060u : 0xFFD0D0D0u);
            y += height;
        }
        put_text(px, w, h, x0 + 16, y0 + ph - 22, "Enter: toggle   Esc: back (restart needed)", 0xFF909090u);
        return;
    }
    put_text(px, w, h, x0 + 16, y0 + 10, s->page == SETTINGS_VIDEO ? "VIDEO SETTINGS" : s->page == SETTINGS_AUDIO ? "AUDIO SETTINGS" : s->page == SETTINGS_ADVANCED ? "ADVANCED SETTINGS" : "SETTINGS", 0xFFFFFFFFu);
    for (int row = s->top; row < s->top + shown && row < total; row++) {
        const int i = rows[row];
        const int y = y0 + 34 + (row - s->top) * 20, on = i == s->cursor;
        const uint32_t color = on ? 0xFFFFE060u : 0xFFD0D0D0u;
        if (on) put_text(px, w, h, x0 + 12, y, ">", color);
        if (i < NDEFS) {
            char v[64];
            const char *val = settings_value(s, i);
            if (!strcmp(DEFS[i].key, "present-scale")) {
                /* Picture detail is a multiplier on the original's 320 x 200: show the picture it makes (the stored value stays the number) */
                const int is_auto = !strcmp(val, "auto");
                const int n = is_auto ? s_auto_scale : atoi(val);
                if (n < 1 || n > 16) snprintf(v, sizeof v, "< %s >", val);
                else if (is_auto) snprintf(v, sizeof v, "< auto (%d: %dx%d) >", n, 320 * n, 200 * n);
                else snprintf(v, sizeof v, "< %d (%dx%d) >", n, 320 * n, 200 * n);
            } else snprintf(v, sizeof v, "< %s >", val);
            put_text(px, w, h, x0 + 30, y, DEFS[i].label, color);
            put_text(px, w, h, x0 + 200, y, v, s->custom[i][0] || s->value[i] != DEFS[i].def ? 0xFF60B0FFu : color);
            if (!DEFS[i].live && changed(s,i)) put_text(px, w, h, x0 + 370, y, "*", 0xFF60E060u);
        } else put_text(px, w, h, x0 + 30, y, i == NDEFS ? "Save" : i == NDEFS + 1 ? "Watch last flight" : i == NDEFS + 2 ? "Remap a key..." :
                  i == NDEFS + 3 ? "Clear key remaps" : i == NDEFS + 4 ? "Choose fixes..." : i == NDEFS + 5 ? "Watch a flight..." : i == NDEFS + 6 ? "Folders..." : i == NDEFS + 7 ? "Controller button as key..." :
                  i == NDEFS + 8 ? "Clear controller keys" : i == NDEFS + 10 ? "Advanced settings..." : i == NDEFS + 11 ? "Audio settings..." :
                  i == NDEFS + 14 ? "Video settings..." : i == NDEFS + 12 ? "Reset all settings to default" : i == NDEFS + 13 ? "Back" : "Close without saving", color);
    }
    if (s->top > 0) put_text(px, w, h, x0 + pw - 40, y0 + 10, "^", 0xFF909090u);
    if (s->top + shown < total) put_text(px, w, h, x0 + pw - 24, y0 + 10, "v", 0xFF909090u);
    put_wrapped(px, w, h, x0 + 16, y0 + ph - 86, s->cursor < NDEFS ? settings_description(DEFS[s->cursor].key) : action_help(s->cursor - NDEFS), 46, 3, 0xFFB0B0B0u);
    const int fy = y0 + ph - 38;
    if (s->padkey_stage) {
        put_text(px, w, h, x0 + 16, fy, s->padkey_stage == 1 ? "Press the controller button (Esc: cancel)" : "Press the key it should act as", 0xFFFFE060u);
        put_text(px, w, h, x0 + 16, fy + 16, s->note, 0xFF909090u);
        return;
    }
    put_text(px, w, h, x0 + 16, fy, "Up/Down choose, Left/Right change,", 0xFF909090u);
    put_text(px, w, h, x0 + 16, fy + 16, s->note[0] ? s->note : s->page==SETTINGS_MAIN ? "* applies after restart. ` closes." : "* applies after restart. Esc: back.", 0xFF909090u);
}
