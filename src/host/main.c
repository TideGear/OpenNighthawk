/* main.c - the game in a window: SDL3 video, input, audio and MIDI around
 * the emulated PC.
 *
 *   f117a [--data DIR] [--save DIR] [--engine recomp|interp] [--ips N] [--timing dosbox|386]
 *         [--scale N] [--fullscreen] [--no-aspect] [--midi N] [--log FILE]
 *         [--audio-queue-log FILE]
 *         [--audio-dump FILE] [--present scan|replay|interp] [--present-scale N]
 *         [--present-age interp|extrapolate] [--present-log FILE] [--config FILE | --no-config]
 *
 * Every option can also be kept in f117a.ini beside the executable (or the
 * file --config names): see config.h. The command line overrides it.
 *
 * The machine runs on its own clock (instructions); this loop keeps that
 * clock level with the wall clock, hands it the keyboard, mouse and stick
 * between slices, plays the audio it produced and shows the last frame its
 * VGA scanned out. None of that changes what the program computes: the
 * same inputs at the same instruction counts give the same run.
 *
 * --present replay shows, in flight, the Stage 1 replay of the original's
 * draw records (src/present/drawfeed.h) in place of the scanned-out picture:
 * the same picture at 320x200, each logic frame from its close, and the
 * scanned one wherever the replay does not hold the screen. --present interp
 * shows the in-between frames of the last two logic frames (Stage 3) at the
 * display's own rate: each is drawn just before the frame is presented (with
 * vsync), at the machine's clock then, a logic step behind (--present-age
 * extrapolate: predicted past the newer one instead, no step behind).
 * --present-scale N draws the replayed picture N times finer (Stage 2): the
 * model polygons from their vertices, the rest scaled. --present-log FILE
 * writes, for each frame presented, the host clock, the machine's clock, the
 * logic frames shown and t.
 *
 * Host keys (chosen not to collide with the game's own bindings):
 *   Alt+Enter          toggle fullscreen
 *   Ctrl+Alt+F12       quit
 *   Ctrl+Alt+P         pause / resume
 */
#include "machine.h"
#include "present.h"
#include "audio.h"
#include "midistream.h"
#include "recomp_rt.h"
#include "fixes.h"
#include "inputlog.h"
#include "host_clock.h"
#include "config.h"
#include "mt32.h"
#include "drawfeed.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

typedef struct {
    machine_t       m;
    uint8_t        *mem;
    audio_t        *audio;
    present_frame   frame;            /* the last frame the VGA scanned out */
    int             have_frame;
    int             audio_failed;
    midistream      midi;
    FILE           *record;           /* --record: every input as it is applied */
    int             roland_out;       /* Roland MIDI goes somewhere: Munt or a Windows MIDI device */
    int             roland_unheard;   /* the game sent Roland MIDI with nowhere to play it */
#ifdef _WIN32
    HMIDIOUT        midi_out;
#endif
} host_t;

static host_t H;

/* --present replay */
static int g_replay;                  /* 1 replay, 2 interp */
static drawfeed g_feed;
static drawlive g_live;
static uint64_t g_presented;          /* VGA frames shown from the replay */
static int g_scale, g_age;            /* --present-scale, --present-age (1 extrapolate) */
static const uint8_t *g_fine;         /* the finer picture to show, or NULL */
static FILE *g_present_log;           /* --present-log */

/* ---- machine hooks ------------------------------------------------------ */

static void on_opl(void *u, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)u;
    if (H.audio) audio_opl_write(H.audio, icount, reg, val);
}

static void on_speaker(void *u, uint64_t icount)
{
    (void)u;
    if (H.audio) audio_speaker(H.audio, &H.m, icount);
}

static void on_input(void *u, const machine_input *in)
{
    (void)u;
    if (H.record) {
        inputlog_write(H.record, in);
        fflush(H.record);          /* a session killed mid-flight keeps its log */
    }
}

static void on_vsync(void *u, uint64_t icount)
{
    (void)u; (void)icount;
    present_capture(&H.m, &H.frame);
    if (g_replay == 1) {                  /* interp is drawn when the host presents */
        drawlive_update(&g_live, &g_feed);
        g_presented += (uint64_t)(g_replay == 2 ? drawlive_present_interp(&g_live, &H.m, &H.frame)
                                                : drawlive_present(&g_live, &H.m, &H.frame));
        g_fine = g_live.shown_hi;
    }
    H.have_frame = 1;
}

#ifdef _WIN32
static void midi_send(void *user, const uint8_t *msg, size_t len)
{
    (void)user;
    if (!H.midi_out) return;
    if (msg[0] == 0xF0) {
        MIDIHDR hdr;
        memset(&hdr, 0, sizeof hdr);
        hdr.lpData = (LPSTR)msg;
        hdr.dwBufferLength = (DWORD)len;
        if (midiOutPrepareHeader(H.midi_out, &hdr, sizeof hdr) != MMSYSERR_NOERROR) return;
        midiOutLongMsg(H.midi_out, &hdr, sizeof hdr);
        while (!(hdr.dwFlags & MHDR_DONE)) Sleep(0);
        midiOutUnprepareHeader(H.midi_out, &hdr, sizeof hdr);
        return;
    }
    DWORD word = msg[0];
    if (len > 1) word |= (DWORD)msg[1] << 8;
    if (len > 2) word |= (DWORD)msg[2] << 16;
    midiOutShortMsg(H.midi_out, word);
}
#endif

static void on_midi(void *u, uint64_t icount, uint8_t b)
{
    (void)u;
    if (!H.roland_out && !H.roland_unheard) H.roland_unheard = 1;
    if (H.audio && !audio_midi_byte(H.audio, icount, b)) H.audio_failed = 1;
#ifdef _WIN32
    midistream_byte(&H.midi, b, midi_send, NULL);
#else
    (void)b;
#endif
}

/* ---- input --------------------------------------------------------------- */

/* An SDL (USB HID) scancode as the PC set-1 byte a DOS keyboard sends, with
 * 0x100 set for a grey key that arrives after an E0 prefix. 0 for keys a
 * 1991 AT keyboard does not have. (The Reimp's table, platform_sdl3.c.) */
#include "host_keys.h"

static void key_event(SDL_Scancode s, int down)
{
    if (s == SDL_SCANCODE_PAUSE) {
        if (!down) return;
        static const uint8_t pause[6] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
        for (int i = 0; i < 6; i++) machine_key_byte(&H.m, pause[i]);
        return;
    }
    const unsigned code = pc_scancode(s);
    if (!code) return;
    if (code & 0x100) machine_key_byte(&H.m, 0xE0);
    machine_key_byte(&H.m, (uint8_t)((code & 0x7F) | (down ? 0 : 0x80)));
}

/* ---- the picture ---------------------------------------------------------- */

static SDL_FRect output_rect(SDL_Renderer *ren, int aspect)
{
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(ren, &ow, &oh);
    /* The original filled a 4:3 monitor: 320x200 pixels are 1.2 times as
     * tall as they are wide. */
    float want = aspect ? 4.0f / 3.0f : (H.frame.text ? 640.0f / 400.0f : 320.0f / 200.0f);
    float w = (float)ow, h = (float)ow / want;
    if (h > (float)oh) { h = (float)oh; w = h * want; }
    SDL_FRect r = { ((float)ow - w) / 2.0f, ((float)oh - h) / 2.0f, w, h };
    return r;
}

static void window_to_guest(SDL_Renderer *ren, int aspect, float wx, float wy, int *gx, int *gy)
{
    float rx = wx, ry = wy;
    SDL_RenderCoordinatesFromWindow(ren, wx, wy, &rx, &ry);
    SDL_FRect r = output_rect(ren, aspect);
    float u = (rx - r.x) / r.w, v = (ry - r.y) / r.h;
    if (u < 0) u = 0;
    if (u > 0.9999f) u = 0.9999f;
    if (v < 0) v = 0;
    if (v > 0.9999f) v = 0.9999f;
    *gx = (int)(u * 320.0f);
    *gy = (int)(v * 200.0f);
}

/* ---- main ------------------------------------------------------------------ */

static const char *default_data_dir(void)
{
    static const char *candidates[] = {
        "D:\\GOG\\F-117A", "C:\\GOG Games\\F-117A", "C:\\Program Files (x86)\\GOG Galaxy\\Games\\F-117A",
        /* Steam keeps the game in a subfolder; its files are identical to GOG's. */
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\F-117A Nighthawk Stealth Fighter\\F-117A",
        "C:\\Program Files\\Steam\\steamapps\\common\\F-117A Nighthawk Stealth Fighter\\F-117A",
        ".", NULL };
    for (int i = 0; candidates[i]; i++) {
        char p[600];
        snprintf(p, sizeof p, "%s\\F117.COM", candidates[i]);
        FILE *f = fopen(p, "rb");
        if (f) { fclose(f); return candidates[i]; }
    }
    return NULL;
}

/* An MT-32 control and PCM ROM pair in a folder, recognised by content
 * (mt32_find_roms: Munt's SHA-1 table), whatever the files are called. */
static int find_mt32_roms(const char *dir, const char **ctrl, const char **pcm, char *what, size_t what_size)
{
    int count = 0;
    char **names = SDL_GlobDirectory(dir, NULL, 0, &count);
    if (!names) return 0;
    static char paths[256][1024];
    const char *files[256];
    int n = 0;
    const size_t dl = strlen(dir);
    const char *sep = dl && (dir[dl - 1] == '\\' || dir[dl - 1] == '/') ? "" : "\\";
    for (int i = 0; i < count && n < 256; i++) {
        snprintf(paths[n], sizeof paths[n], "%s%s%s", dir, sep, names[i]);
        SDL_PathInfo info;
        if (SDL_GetPathInfo(paths[n], &info) && info.type == SDL_PATHTYPE_FILE && info.size <= 1048576) {
            files[n] = paths[n];
            n++;
        }
    }
    SDL_free(names);
    return mt32_find_roms(files, n, ctrl, pcm, what, what_size);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *log_path = NULL;
    const char *audio_queue_log_path = NULL;
    const char *audio_dump_path = NULL;
    uint64_t ips = MACHINE_DEFAULT_IPS;
    int scale = 3, fullscreen = 0, aspect = 1, midi_dev = -2;
    int engine = ENGINE_RECOMP;
    const char *coverage = NULL, *record = NULL, *replay = NULL;
    uint64_t time_us = 0, exit_after = 0;
    audio_opl_backend opl_backend = AUDIO_OPL_DBOPL;
    int speaker_model = 0;                  /* speaker.h: realsound */
    int no_record = 0;
    const char *mt32_control = NULL, *mt32_pcm = NULL;
    unsigned int mt32_seed = 0;
    int mt32_seed_set = 0;

    /* f117a.ini (beside the executable, or --config FILE): its options go
     * ahead of the command line's, so the command line wins. */
    {
        int explicit_ = 0;
        const char *ini = config_path(argc, argv, SDL_GetBasePath(), &explicit_);
        if (ini) {
            char *text = config_read_file(ini);
            char err[300], dir[1024], **merged = NULL;
            if (!text) {
                snprintf(err, sizeof err, "Cannot read the configuration file %s", ini);
                fprintf(stderr, "%s\n", err);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", err, NULL);
                return 2;
            }
            snprintf(dir, sizeof dir, "%s", ini);
            char *slash = strrchr(dir, '\\'), *fwd = strrchr(dir, '/');
            if (fwd > slash) slash = fwd;
            if (slash) slash[1] = 0; else dir[0] = 0;
            const int n = config_merge(text, dir, argc, argv, &merged, err, sizeof err);
            free(text);
            if (n < 0) {
                fprintf(stderr, "%s (%s)\n", err, ini);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", err, NULL);
                return 2;
            }
            argc = n;
            argv = merged;
        }
    }
    int from_cli = 0, cli_roland = 0;
    enum { ROLAND_UNSET, ROLAND_MUNT, ROLAND_WINDOWS, ROLAND_OFF } roland = ROLAND_UNSET;
    const char *mt32_roms = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, CONFIG_CLI_MARK)) { from_cli = 1; continue; }
        /* a Roland output named on the command line replaces the file's */
        if (from_cli && !cli_roland && (!strcmp(a, "--midi") || !strcmp(a, "--mt32-control") || !strcmp(a, "--mt32-pcm") ||
                                        !strcmp(a, "--roland") || !strcmp(a, "--mt32-roms"))) {
            cli_roland = 1;
            roland = ROLAND_UNSET; midi_dev = -2;
            mt32_control = mt32_pcm = mt32_roms = NULL;
        }
        if (!strcmp(a, "--roland") && v) {
            if (!strcmp(v, "munt")) roland = ROLAND_MUNT;
            else if (!strcmp(v, "windows")) roland = ROLAND_WINDOWS;
            else if (!strcmp(v, "off")) roland = ROLAND_OFF;
            else { fprintf(stderr, "--roland takes munt, windows or off\n"); return 2; }
            i++; continue;
        }
        if (!strcmp(a, "--mt32-roms") && v) { mt32_roms = v; i++; continue; }
        if (!strcmp(a, "--config") && v) { i++; continue; }
        if (!strcmp(a, "--no-config")) continue;
        if (!strcmp(a, "--data") && v) { data = v; i++; }
        else if (!strcmp(a, "--record") && v) { record = v; i++; }
        else if (!strcmp(a, "--no-record")) no_record = 1;
        else if (!strcmp(a, "--replay") && v) { replay = v; i++; }
        else if (!strcmp(a, "--time-us") && v) { time_us = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--exit-after") && v) { exit_after = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--opl") && v) {
            if (!strcmp(v, "dbopl")) opl_backend = AUDIO_OPL_DBOPL;
            else if (!strcmp(v, "nuked")) opl_backend = AUDIO_OPL_NUKED;
            else { fprintf(stderr, "--opl requires dbopl or nuked\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--speaker") && v) {
            if (!strcmp(v, "realsound")) speaker_model = 0;
            else if (!strcmp(v, "pwm")) speaker_model = 1;
            else { fprintf(stderr, "--speaker takes realsound or pwm\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--save") && v) { save = v; i++; }
        else if (!strcmp(a, "--log") && v) { log_path = v; i++; }
        else if (!strcmp(a, "--audio-queue-log") && v) { audio_queue_log_path = v; i++; }
        else if (!strcmp(a, "--audio-dump") && v) { audio_dump_path = v; i++; }
        else if (!strcmp(a, "--ips") && v) { ips = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--timing") && v) {
            /* dosbox (the default): GOG DOSBox's 9 million instructions a second, at which the game
             * already draws its most frames a second in flight (docs/bugs.md D1); 386: the 386DX/33
             * profile, the pace of a 1991 PC (src/cpu/timing386.h) */
            if (!strcmp(v, "386")) { _putenv_s("F117R_TIMING", "386"); ips = MACHINE_386_IPS; }
            else if (strcmp(v, "dosbox")) { fprintf(stderr, "--timing takes dosbox or 386\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--midi") && v) { midi_dev = atoi(v); i++; }
        else if (!strcmp(a, "--mt32-control") && v) { mt32_control = v; i++; }
        else if (!strcmp(a, "--mt32-pcm") && v) { mt32_pcm = v; i++; }
        else if (!strcmp(a, "--mt32-seed") && v) {
            mt32_seed = (unsigned int)strtoul(v, NULL, 0); mt32_seed_set = 1; i++;
        }
        else if (!strcmp(a, "--engine") && v) { engine = !strcmp(v, "interp") ? ENGINE_INTERP : ENGINE_RECOMP; i++; }
        else if (!strcmp(a, "--coverage") && v) { coverage = v; i++; }
        else if (!strcmp(a, "--fix") && v) {
            if (!fixes_enable(v, 1)) { fprintf(stderr, "no fix %s (--list-fixes)\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--list-fixes")) { fixes_list(stdout); return 0; }
        else if (!strcmp(a, "--present") && v) {
            if (strcmp(v, "replay") && strcmp(v, "scan") && strcmp(v, "interp")) {
                fprintf(stderr, "--present takes scan, replay or interp\n");
                return 2;
            }
            g_replay = !strcmp(v, "replay") ? 1 : !strcmp(v, "interp") ? 2 : 0;
            i++;
        }
        else if (!strcmp(a, "--present-log") && v) {
            g_present_log = fopen(v, "w");
            if (!g_present_log) { fprintf(stderr, "cannot write %s\n", v); return 2; }
            fprintf(g_present_log, "host_ns icount older newer t replayed\n");
            i++;
        }
        else if (!strcmp(a, "--present-scale") && v) {
            g_scale = atoi(v);
            if (g_scale < 1 || g_scale > 16) { fprintf(stderr, "--present-scale takes 1 to 16\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--present-age") && v) {
            if (strcmp(v, "interp") && strcmp(v, "extrapolate")) { fprintf(stderr, "--present-age takes interp or extrapolate\n"); return 2; }
            g_age = !strcmp(v, "extrapolate");
            i++;
        }
        else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
        else if (!strcmp(a, "--no-aspect")) aspect = 0;
        else {
            fprintf(stderr,
                "usage: f117a [--data DIR] [--save DIR] [--engine recomp|interp] [--ips N] [--timing dosbox|386]\n"
                "             [--scale N] [--fullscreen] [--no-aspect] [--midi N] [--log FILE]\n"
                "             [--record FILE | --no-record] [--replay FILE] [--time-us N]\n"
                "             [--exit-after CLOCKS] [--opl dbopl|nuked] [--speaker realsound|pwm]\n"
                "             [--audio-queue-log FILE]\n"
                "             [--audio-dump FILE] [--present scan|replay|interp] [--present-scale N]\n"
                "             [--present-age interp|extrapolate] [--present-log FILE]\n"
                "             [--roland munt|windows|off] [--mt32-roms DIR]\n"
                "             [--mt32-control FILE --mt32-pcm FILE] [--fix ID|all]... [--list-fixes]\n"
                "             [--config FILE | --no-config]   (default: f117a.ini beside f117a.exe)\n");
            return 2;
        }
    }
    /* --roland and --mt32-roms: the simple form of the Roland options */
    if (roland == ROLAND_WINDOWS) {
        mt32_control = mt32_pcm = NULL;
        if (midi_dev == -2) midi_dev = -1;                    /* the MIDI mapper */
    } else if (roland == ROLAND_OFF) {
        mt32_control = mt32_pcm = NULL;
        midi_dev = -2;
    } else if (roland == ROLAND_MUNT || (roland == ROLAND_UNSET && mt32_roms && midi_dev == -2)) {
        midi_dev = -2;
        if (!mt32_control || !mt32_pcm) {
            const char *ctrl = NULL, *pcm = NULL;
            char what[120];
            if (!mt32_roms || !find_mt32_roms(mt32_roms, &ctrl, &pcm, what, sizeof what)) {
                char msg[1300];
                snprintf(msg, sizeof msg, mt32_roms
                    ? "No MT-32 control ROM with its PCM ROM found in %s. Munt recognises the ROMs by content "
                      "(MT-32 control 1.04-1.07 or 2.03-2.07, CM-32L), whatever the files are called."
                    : "Roland through Munt needs your MT-32 ROMs: set mt32-roms to the folder holding them.%s",
                    mt32_roms ? mt32_roms : "");
                fprintf(stderr, "%s\n", msg);
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", msg, NULL);
                return 2;
            }
            fprintf(stderr, "Roland: Munt with %s (%s, %s)\n", what, ctrl, pcm);
            mt32_control = ctrl; mt32_pcm = pcm;
        }
    }
    if ((mt32_control != NULL) != (mt32_pcm != NULL) || (mt32_control && midi_dev != -2)) {
        fprintf(stderr, "Supply both --mt32-control and --mt32-pcm; choose one Roland output\n");
        return 2;
    }
    if (mt32_seed_set && !mt32_control) {
        fprintf(stderr, "--mt32-seed requires --mt32-control and --mt32-pcm\n");
        return 2;
    }
    if (!data) data = default_data_dir();
    if (!data) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A",
            "Could not find the game. Point --data at the folder holding F117.COM "
            "(your own GOG, Steam or original installation).", NULL);
        return 1;
    }
    char save_buf[700];
    if (!save) {
        const char *base = SDL_GetPrefPath("F117Recomp", "F-117A");
        snprintf(save_buf, sizeof save_buf, "%ssave", base ? base : "./");
        save = save_buf;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    if (!SDL_CreateWindowAndRenderer("F-117A Nighthawk Stealth Fighter 2.0",
                                     320 * scale, 240 * scale,
                                     SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0),
                                     &win, &ren)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, 640, 400);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    /* the finer picture (--present-scale) has a texture of its own size */
    SDL_Texture *tex_fine = NULL;
    uint32_t *fine_pixels = NULL;
    if (g_replay && g_scale > 1) {
        tex_fine = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320 * g_scale, 200 * g_scale);
        fine_pixels = (uint32_t *)malloc(sizeof *fine_pixels * 320 * g_scale * 200 * g_scale);
        if (tex_fine) SDL_SetTextureScaleMode(tex_fine, SDL_SCALEMODE_LINEAR);
    }
    SDL_HideCursor();

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, AUDIO_RATE };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (stream) SDL_ResumeAudioStreamDevice(stream);
    /* A replay's clock must be known before creating either audio backend. */
    if (replay) {
        inputlog_read_header(replay, &ips, &time_us);
        char ids[256];
        inputlog_read_fixes(replay, ids, sizeof ids);
        if (!fixes_enable_list(ids)) {
            fprintf(stderr, "%s names an unknown fix: %s\n", replay, ids);
            SDL_Quit(); return 2;
        }
    }
    H.audio = audio_create_backend(ips, opl_backend);
    if (H.audio) audio_set_speaker_model(H.audio, speaker_model);
    if (mt32_control) {
        char error[256];
        if (mt32_seed_set) srand(mt32_seed);
        if (!audio_enable_mt32(H.audio, mt32_control, mt32_pcm, error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            audio_destroy(H.audio); SDL_Quit(); return 1;
        }
        H.roland_out = 1;
    }

#ifdef _WIN32
    if (midi_dev != -2) {
        UINT id = midi_dev < 0 ? MIDI_MAPPER : (UINT)midi_dev;
        if (midiOutOpen(&H.midi_out, id, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) H.midi_out = NULL;
        else H.roland_out = 1;
    }
#endif
    midistream_init(&H.midi);

    SDL_Gamepad *pad = NULL;
    SDL_Joystick *joy = NULL;
    {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetJoysticks(&n);
        if (ids && n > 0) {
            if (SDL_IsGamepad(ids[0])) pad = SDL_OpenGamepad(ids[0]);
            else joy = SDL_OpenJoystick(ids[0]);
        }
        SDL_free(ids);
    }

    H.mem = (uint8_t *)malloc(MEM_SIZE);
    H.m.log = log_path ? fopen(log_path, "w") : NULL;
    H.m.engine = engine;
    recomp_init(&H.m);
    if (coverage) recomp_set_coverage(&H.m, coverage);
    machine_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.opl_write = on_opl;
    hooks.speaker = on_speaker;
    hooks.vsync = on_vsync;
    hooks.midi_byte = on_midi;
    hooks.module_load = recomp_module_load;
    hooks.file_data = fixes_file_data;
    if (g_replay) {
        drawfeed_init(&g_feed, H.mem);
        drawlive_init(&g_live);
        g_live.interp = g_replay == 2;
        g_live.extrapolate = g_age;
        if (g_scale) drawlive_set_scale(&g_live, g_scale);
        observe_set(&g_feed.obs);
    }
    /* A replay brings its own speed and boot time; a recording writes ours. */
    if (!time_us) time_us = machine_local_time_us();
    if (!machine_boot(&H.m, H.mem, data, save, "F117.COM", ips, time_us, &hooks)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", H.m.fault, win);
        return 1;
    }
    /* Every live session is recorded unless told otherwise: a log costs a
     * few kilobytes, and it is what lets any session be replayed under both
     * engines later (tools/run_route.py style, f117run --replay). */
    char auto_record[900];
    if (!record && !replay && !no_record) {
        /* sessions/session-<time>/: input.log, and save-start/ - the save
         * folder as the session found it, which a replay must start from. */
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);
        char dir[800], start_dir[850];
        snprintf(dir, sizeof dir, "%s/../sessions/session-%04d%02d%02d-%02d%02d%02d", save,
                 lt ? lt->tm_year + 1900 : 0, lt ? lt->tm_mon + 1 : 0, lt ? lt->tm_mday : 0,
                 lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
        snprintf(start_dir, sizeof start_dir, "%s/save-start", dir);
        SDL_CreateDirectory(start_dir);
        int n = 0;
        char **names = SDL_GlobDirectory(save, NULL, 0, &n);
        for (int k = 0; names && k < n; k++) {
            char from[900], to[900];
            snprintf(from, sizeof from, "%s/%s", save, names[k]);
            snprintf(to, sizeof to, "%s/%s", start_dir, names[k]);
            SDL_CopyFile(from, to);
        }
        SDL_free(names);
        snprintf(auto_record, sizeof auto_record, "%s/input.log", dir);
        record = auto_record;
        char note[900];
        snprintf(note, sizeof note, "%s/replay.txt", dir);
        FILE *nf = fopen(note, "w");
        if (nf) {
            fprintf(nf, "Replay this session headless under either engine:\n\n"
                        "  f117run --engine recomp --data \"%s\" --save <a copy of save-start> "
                        "--replay input.log --steps <clocks>\n\nor in the window:\n\n"
                        "  f117a --data \"%s\" --save <a copy of save-start> --replay input.log\n",
                    data, data);
            fclose(nf);
        }
    }
    if (record) {
        H.record = fopen(record, "w");
        if (H.record) {
            inputlog_header(H.record, ips, time_us);
            char ids[256];
            fixes_enabled(ids, sizeof ids);
            inputlog_fixes(H.record, ids);
            H.m.on_input = on_input;
        }
    }
    inputlog_reader *player = NULL;
    if (replay && !(player = inputlog_open(replay))) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", "Cannot read the replay file.", win);
        return 1;
    }
    /* Live input during a replay would make it a different session. */
    const int live = player == NULL;
    unsigned last_axis[2] = { 0x200, 0x200 }, last_buttons = 0x100;
    if (stream) {
        /* 60 ms of silence first, so the device never waits on the first
         * frames the machine produces. */
        static int16_t silence[AUDIO_RATE / 1000 * 60 * 2];
        SDL_PutAudioStreamData(stream, silence, (int)sizeof silence);
    }

    static uint32_t pixels[640 * 400];
    static int16_t abuf[AUDIO_RATE * 2];
    FILE *audio_queue_log = audio_queue_log_path ? fopen(audio_queue_log_path, "w") : NULL;
    FILE *audio_dump = audio_dump_path ? fopen(audio_dump_path, "wb") : NULL;
    if (audio_queue_log_path && !audio_queue_log)
        fprintf(stderr, "cannot write audio queue log %s\n", audio_queue_log_path);
    if (audio_dump_path && !audio_dump)
        fprintf(stderr, "cannot write audio dump %s\n", audio_dump_path);
    if (audio_queue_log)
        fputs("host_ns icount target_clock discarded_clocks queued_before_bytes produced_frames queued_after_bytes cleared\n",
              audio_queue_log);
    if (audio_queue_log && mt32_seed_set)
        fprintf(audio_queue_log, "# mt32_seed=%u\n", mt32_seed);
    const uint64_t start_ns = SDL_GetTicksNS();
    uint64_t paused_ns = 0, pause_began = 0;
    uint64_t discarded_clocks = 0;
    int running = 1, paused = 0;
    int mouse_buttons = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
                const int down = ev.type == SDL_EVENT_KEY_DOWN;
                const SDL_Keymod mod = ev.key.mod;
                if (down && ev.key.scancode == SDL_SCANCODE_RETURN && (mod & SDL_KMOD_ALT) && !(mod & SDL_KMOD_CTRL)) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen);
                    break;
                }
                if (down && (mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_ALT)) {
                    if (ev.key.scancode == SDL_SCANCODE_F12) { running = 0; break; }
                    if (ev.key.scancode == SDL_SCANCODE_P) {
                        paused = !paused;
                        if (paused) pause_began = SDL_GetTicksNS();
                        else paused_ns += SDL_GetTicksNS() - pause_began;
                        break;
                    }
                }
                if (!paused && live) key_event(ev.key.scancode, down);   /* repeats re-send the make code */
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                if (!live) break;
                int gx, gy;
                window_to_guest(ren, aspect, ev.motion.x, ev.motion.y, &gx, &gy);
                machine_mouse(&H.m, gx, gy, mouse_buttons, (int)ev.motion.xrel, (int)ev.motion.yrel);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (!live) break;
                int bit = ev.button.button == SDL_BUTTON_LEFT ? 1 : ev.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_buttons |= bit; else mouse_buttons &= ~bit;
                int gx, gy;
                window_to_guest(ren, aspect, ev.button.x, ev.button.y, &gx, &gy);
                machine_mouse(&H.m, gx, gy, mouse_buttons, 0, 0);
                break;
            }
            default: break;
            }
        }
        if (!running) break;

        /* The analog stick on the game port: axes 0..255, buttons. */
        if ((pad || joy) && live) {
            float ax = 0, ay = 0;
            unsigned b = 0;
            if (pad) {
                ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) b |= 1;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) b |= 2;
            } else {
                ax = SDL_GetJoystickAxis(joy, 0) / 32767.0f;
                ay = SDL_GetJoystickAxis(joy, 1) / 32767.0f;
                if (SDL_GetJoystickButton(joy, 0)) b |= 1;
                if (SDL_GetJoystickButton(joy, 1)) b |= 2;
            }
            unsigned axis[4] = { (unsigned)((ax + 1.0f) * 127.5f), (unsigned)((ay + 1.0f) * 127.5f), 0x100, 0x100 };
            if (axis[0] != last_axis[0] || axis[1] != last_axis[1] || b != last_buttons) {
                machine_joystick(&H.m, 1, axis, b);
                last_axis[0] = axis[0]; last_axis[1] = axis[1]; last_buttons = b;
            }
        }

        /* Keep the machine's clock level with the wall clock. */
        if (!paused) {
            const uint64_t now_ns = SDL_GetTicksNS() - start_ns - paused_ns;
            /* Never more than 100 ms behind: after a stall, catch up gently
             * rather than racing. */
            uint64_t target = host_clock_target(now_ns, ips, H.m.cpu.icount, &discarded_clocks);
            if (exit_after && target > exit_after) target = exit_after;
            while (H.m.cpu.icount < target) {
                uint64_t until = H.m.cpu.icount + ips / 1000;  /* 1 ms slices */
                if (until > target) until = target;
                inputlog_feed(player, &H.m, until + ips);
                int rc = machine_run(&H.m, until);
                if (rc == RUN_EXITED) { running = 0; break; }
                if (rc == RUN_FAULT) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A: the machine stopped", H.m.fault, win);
                    running = 0;
                    break;
                }
            }
            if (H.audio) {
                audio_advance(H.audio, H.m.cpu.icount);
                const int queued_before = stream ? SDL_GetAudioStreamQueued(stream) : 0;
                size_t produced_frames = 0;
                size_t n;
                while ((n = audio_take(H.audio, abuf, AUDIO_RATE)) > 0) {
                    if (audio_dump && fwrite(abuf, 4, n, audio_dump) != n)
                        H.audio_failed = 1;
                    if (stream) {
                        SDL_PutAudioStreamData(stream, abuf, (int)(n * 4));
                        produced_frames += n;
                    }
                }
                /* The machine runs on the wall clock and the device on its
                 * own crystal; if the two drift apart by more than a
                 * quarter second, drop the backlog rather than lag. */
                const int queued_after = stream ? SDL_GetAudioStreamQueued(stream) : 0;
                const int cleared = stream && queued_after > AUDIO_RATE * 4 / 4;
                if (cleared)
                    SDL_ClearAudioStream(stream);
                if (audio_queue_log) {
                    fprintf(audio_queue_log, "%llu %llu %llu %llu %d %llu %d %d\n",
                            (unsigned long long)(SDL_GetTicksNS() - start_ns),
                            (unsigned long long)H.m.cpu.icount,
                            (unsigned long long)target,
                            (unsigned long long)discarded_clocks,
                            queued_before,
                            (unsigned long long)produced_frames,
                            queued_after, cleared);
                    if (cleared) fflush(audio_queue_log);
                }
            }
            if (H.audio_failed || (exit_after && H.m.cpu.icount >= exit_after)) running = 0;
        }

        /* Show the last frame the VGA scanned out, or with --present interp the
         * in-between frame at the machine's clock now. */
        if (!H.have_frame) present_capture(&H.m, &H.frame);
        int got = 0;
        double t_shown = 0;
        uint64_t older = 0, newer = 0;
        if (g_replay == 2) {
            present_capture(&H.m, &H.frame);
            drawlive_update(&g_live, &g_feed);
            got = drawlive_present_interp(&g_live, &H.m, &H.frame);
            g_presented += (uint64_t)got;
            g_fine = g_live.shown_hi;
            const drawlive_step *a = &g_live.step[g_live.last ^ 1], *b = &g_live.step[g_live.last];
            const double span = b->end > a->end ? (double)(b->end - a->end) : 0;
            older = a->seq; newer = b->seq;
            t_shown = span > 0 ? ((double)H.m.cpu.icount - (double)b->end) / span : 0.0;
        }
        int w, h;
        SDL_Texture *show = tex;
        if (g_fine && tex_fine && !H.frame.text) {           /* the finer picture, with the frame's palette */
            uint32_t pal[256];
            for (int k = 0; k < 256; k++) {
                const uint8_t *c = &H.frame.dac[3 * k];
                pal[k] = 0xFF000000u | (uint32_t)((c[0] << 2) | (c[0] >> 4)) << 16 |
                         (uint32_t)((c[1] << 2) | (c[1] >> 4)) << 8 | (uint32_t)((c[2] << 2) | (c[2] >> 4));
            }
            w = 320 * g_scale; h = 200 * g_scale;
            for (int k = 0; k < w * h; k++) fine_pixels[k] = pal[g_fine[k]];
            SDL_Rect src = { 0, 0, w, h };
            SDL_UpdateTexture(tex_fine, &src, fine_pixels, w * 4);
            show = tex_fine;
        } else {
            present_render(&H.frame, pixels, &w, &h, (int)((SDL_GetTicksNS() / 266666666ull) & 1));
            SDL_Rect src = { 0, 0, w, h };
            SDL_UpdateTexture(tex, &src, pixels, w * 4);
        }
        SDL_FRect srcf = { 0, 0, (float)w, (float)h };
        SDL_FRect dst = output_rect(ren, aspect);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, show, &srcf, &dst);
        SDL_RenderPresent(ren);
        if (g_present_log)                                    /* when the frame went out, and what it showed */
            fprintf(g_present_log, "%llu %llu %llu %llu %.6f %d\n", (unsigned long long)(SDL_GetTicksNS() - start_ns),
                    (unsigned long long)H.m.cpu.icount, (unsigned long long)older, (unsigned long long)newer, t_shown, got);
        /* Roland chosen in the game with no MIDI output: say so once, rather
         * than play an unexplained silence. */
        if (H.roland_unheard == 1) {
            H.roland_unheard = 2;
            static const char *msg = "Roland music has no output: in f117a.ini set roland = munt and mt32-roms to "
                                     "the folder with your MT-32 ROMs, or roland = windows (Windows MIDI)";
            fputs(msg, stderr);
            fputc(10, stderr);
            SDL_SetWindowTitle(win, "F-117A - no Roland output: set roland in f117a.ini");
        }
    }

    if (H.record) fclose(H.record);
    if (g_present_log) fclose(g_present_log);
    inputlog_close(player);
    if (H.m.log) {
        /* The same summary line f117run prints, so a session can be
         * checked against a headless replay of its log. */
        const uint64_t hsh = machine_state_hash(&H.m);
        fprintf(H.m.log, "stopped at icount %llu; program %s; final hash %016llx\n",
                (unsigned long long)H.m.cpu.icount, dos_current_program(&H.m), (unsigned long long)hsh);
        recomp_report(&H.m, H.m.log);
        if (g_replay)
            fprintf(H.m.log, "[present] replay: %llu logic frames, %llu equal to the display at their close, %llu not; "
                    "%llu VGA frames presented from the replay, %llu of them in-between frames\n", (unsigned long long)g_live.frames,
                    (unsigned long long)g_live.exact, (unsigned long long)g_live.inexact, (unsigned long long)g_presented,
                    (unsigned long long)g_live.inbetweens);
    }
    recomp_shutdown(&H.m);
    machine_shutdown(&H.m);
#ifdef _WIN32
    if (H.midi_out) { midiOutReset(H.midi_out); midiOutClose(H.midi_out); }
#endif
    if (H.m.log) fclose(H.m.log);
    if (audio_queue_log) fclose(audio_queue_log);
    if (audio_dump) fclose(audio_dump);
    audio_destroy(H.audio);
    SDL_Quit();
    return H.audio_failed ? 1 : 0;
}
