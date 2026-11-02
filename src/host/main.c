/* main.c - the game in a window: SDL3 video, input, audio and MIDI around
 * the emulated PC.
 *
 *   f117a [--data DIR] [--save DIR] [--engine recomp|interp] [--ips N] [--timing dosbox|386]
 *         [--scale N] [--fullscreen] [--no-aspect] [--midi N] [--log FILE]
 *         [--audio-queue-log FILE]
 *         [--audio-dump FILE] [--present scan|replay|interp|rerun] [--present-scale N]
 *         [--present-line-width N] [--present-age interp|extrapolate] [--present-log FILE] [--config FILE | --no-config]
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
 * model polygons from their vertices, fine HUD strokes, pixel text/art. --present-log FILE
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
#include "settings.h"
#include "crt.h"
#include "colourblind.h"
#include "controls.h"
#include "remap.h"
#include "keyref.h"
#include "subtitles.h"
#include "macsound.h"
#include "sidecar.h"
#include "soundpolicy.h"
#include "remaster_live.h"
#include "font8x16.h"
#ifdef _WIN32
#include <process.h>
#endif
#include "recomp_rt.h"
#include "fixes.h"
#include "inputlog.h"
#include "keys.h"
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
static int g_loop_timing;                 /* F117_LOOP_TIMING */
static uint64_t g_lt_run, g_lt_present, g_lt_last, g_lt_icount, g_lt_disc;
/* Ctrl+Alt+C: the picture as shown, saved as screenshots/<session>-clock<flight clock>.bmp beside the sessions folder: the
 * session names the flight, the clock the moment a replay of it jumps to */
static char g_shots_dir[800], g_session_name[64];
static int g_shot_now;
static unsigned g_lt_iter;
static uint64_t g_lt_hook[3], g_lt_calls[3];   /* ticks and calls: OPL writes, speaker toggles, vsyncs */
static struct { uint32_t lin; uint16_t cs, ip; unsigned n; } g_lt_slow[64];   /* where the machine was when a slice ran slow */
static int g_lt_nslow;
static int g_replay;                  /* 1 replay, 2 interp (with g_rerun: in-between pictures drawn by the original on a copy) */
static int g_rerun;
static drawfeed g_feed;
static drawlive g_live;
static uint64_t g_presented;          /* VGA frames shown from the replay */
static int g_scale, g_age, g_line_width; /* optional presentation settings */
static int g_fit_scale;               /* --presentation enhanced: --present-scale from the display's height */
static int g_subtitles;                /* --subtitles: show the radio calls as text (subtitles.h) */
static subtitles g_sub;
static macsound *g_mac;                /* --mac-sounds DIR: the Macintosh recordings (macsound.h); NULL: none */
static sp_source g_source[SP_KINDS];   /* the player's override per kind (soundpolicy.h): SP_NONE = the default order */

/* Hybrid sound (docs/sound-board.md): the machine's exec hook fires when the game makes its far call into the sound
 * driver (VGAME 1E42:0313, the id the game wants at SS:SP+4). Music, speech and effects each have a default source
 * and the player may override it (--music-source, --sound-source: default, mac or dos). Only the Mac recordings are
 * the host's to play; whatever is not taken here goes on to the DOS driver as the original does. The game's own
 * priority gate has already admitted the request by the time it gets here, so that part of the original is unchanged. */
static int g_remaster;                 /* --presentation remastered */
static uint8_t *g_rbuf; static size_t g_rbuf_cap;
static uint32_t *g_rrgb; static uint8_t *g_rmask; static int g_rtrue;   /* the true colour of the Remastered pixels, when composed */
static remaster_live *g_remaster_src;       /* the frame the last remaster_apply used (the wide view uses the same) */
static remaster_live *g_rlive_copy;       /* Remastered's collector for the rerun copies (rerun.h): the frame of the picture a copy drew */
static remaster_live *g_rlive;         /* F117_REMASTER_FRAME=N:FILE.ppm: Remastered's view of the running game (remaster_live.h) */
static unsigned g_rlive_base;

/* Widescreen Remastered: in a window wider than 4:3 the world fills the window and the game's own 320 x 200 picture (HUD,
 * cockpit) stays at its size in the middle, so nothing is cut off and the crosshair stays at the centre. It is composed in the
 * same pass as the Remastered 3D view (remaster_live_compose). The picture is built in the shape the fine picture has before the
 * 4:3 stretch: 1.2 times the window's width over its height, 200n rows tall. */
static SDL_Renderer *g_ren;            /* the window's renderer, for its output size */
static int g_aspect_on = 1;            /* the 4:3 shape is kept (not --no-aspect) */
static SDL_Texture *g_wtex;
static uint32_t *g_wpix;
static int g_ww, g_wh;
static int g_wide_shown;               /* the last presented picture was the wide one: the mouse maps through it */
static int g_wide_filled;              /* g_wpix holds the picture composed this time */

/* The width, in fine pixels, of the wide picture the window wants, or 0 when it is not wider than 4:3 (then the 4:3 picture is used). */
static int wide_width(void)
{
    if (!g_ren || !g_aspect_on || g_scale < 1) return 0;
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(g_ren, &ow, &oh);
    if (oh <= 0) return 0;
    const double a = (double)ow / (double)oh;
    if (a < 4.0 / 3.0 + 0.04) return 0;
    const int wide_w = (int)(200.0 * g_scale * a * 1.2 + 0.5) & ~1;
    return wide_w < 320 * g_scale || wide_w > 16384 ? 0 : wide_w;
}

/* The texture and the pixels for a wide picture wide_w across (made again when the shape changes). */
static int wide_prepare(int wide_w)
{
    if (g_wtex && g_wpix && g_ww == wide_w) return 1;
    if (g_wtex) SDL_DestroyTexture(g_wtex);
    free(g_wpix);
    g_wtex = NULL; g_wpix = NULL;
    g_ww = wide_w; g_wh = 200 * g_scale;
    g_wtex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, g_ww, g_wh);
    g_wpix = (uint32_t *)malloc(sizeof *g_wpix * (size_t)g_ww * g_wh);
    return g_wtex && g_wpix;
}

/* The Enhanced picture's fine pixels with the 3D view replaced by the Remastered render (remaster_live_composite); the
 * original fine picture when there is nothing to compose. */
static const uint8_t *remaster_apply(const uint8_t *hi)
{
    static int said;
    if (!said && g_remaster && getenv("F117_REMASTER_DEBUG")) {
        said = 1;
        fprintf(stderr, "[remaster] apply: rlive %p hi %p tag %p scale %d\n", (void *)g_rlive, (const void *)hi, (const void *)g_live.shown_tag, g_scale);
    }
    if (!g_remaster || !g_rlive || !hi || !g_live.shown_tag || g_scale < 1) return hi;
    const size_t size = (size_t)320 * g_scale * 200 * g_scale;
    if (g_rbuf_cap < size) {
        free(g_rbuf); free(g_rrgb); free(g_rmask);
        g_rbuf = (uint8_t *)malloc(size);
        g_rrgb = (uint32_t *)malloc(size * sizeof *g_rrgb);
        g_rmask = (uint8_t *)malloc(size);
        g_rbuf_cap = g_rbuf && g_rrgb && g_rmask ? size : 0;
    }
    g_rtrue = 0;
    if (!g_rbuf_cap) return hi;
    /* The frame that belongs to the picture: a rerun copy's own when the picture came from one (the same moment as its mask), else the
     * live machine's last frame (a logic step's own picture). */
    static int no_copy = -1;                              /* F117_REMASTER_NOCOPY (diagnostic): always the live frame, as before the copies */
    if (no_copy < 0) no_copy = getenv("F117_REMASTER_NOCOPY") != NULL;
    remaster_live *src = !no_copy && g_live.shown_copy && g_rlive_copy ? g_rlive_copy : g_rlive;
    if (getenv("F117_REMASTER_COPYCHECK")) {
        static unsigned calls;
        static unsigned every; if (!every) { const int e = atoi(getenv("F117_REMASTER_COPYCHECK")); every = e > 1 ? (unsigned)e : 120; }   /* =N: every Nth picture (a prime avoids the logic-step phase) */
        if (++calls % every == 1) fprintf(stderr, "[copycheck] picture from %s: copy frame %zu primitives, live frame %zu\n", src == g_rlive_copy ? "a copy" : "the live machine", g_rlive_copy ? remaster_live_done_prims(g_rlive_copy) : 0, remaster_live_done_prims(g_rlive));
    }
    g_remaster_src = src;
    g_wide_filled = 0;
    int ww = wide_width();
    if (ww && !wide_prepare(ww)) ww = 0;
    rerun_set_wide(ww ? ww * 130 / (320 * g_scale) : 100);   /* the copies see 30% wider than the canvas shows, so a big object (a hangar) is still asked for while part of it is in view (the cull tests the centre; the side planes, in the copy only) */
    const uint64_t t0 = SDL_GetPerformanceCounter();
    const int composed = remaster_live_compose(src, &H.m, g_scale, hi, g_live.shown_tag, H.frame.dac, g_rbuf, g_rrgb, g_rmask, ww, ww ? g_wpix : NULL);
    g_wide_filled = composed && ww;
    if (getenv("F117_REMASTER_TIMING")) {               /* diagnostic: where a Remastered picture's milliseconds go (stderr, every 120th) */
        static double sum; static unsigned n;
        sum += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        if (++n % 120 == 0) { fprintf(stderr, "[timing] composite %.2f ms (mean of %u)\n", sum / n, n); }
    }
    if (!composed) return hi;
    g_rtrue = 1;
    return g_rbuf;
}

/* After convert_fine has turned the fine picture's palette indices into pixels: the Remastered 3D view in true colour. */
static void remaster_true_colour(uint32_t *pixels, size_t count)
{
    static int said;
    static int calls;
    if (getenv("F117_REMASTER_DEBUG") && ++calls % 40 == 0 && said < 6) {
        said++;
        size_t set = 0; for (size_t i = 0; i < count; i++) set += g_rmask ? g_rmask[i] : 0;
        fprintf(stderr, "[overlay] true %d mask %p rgb %p count %zu mask-set %zu\n", g_rtrue, (void *)g_rmask, (void *)g_rrgb, count, set);
    }
    if (!g_rtrue || !g_rmask || !g_rrgb) return;
    for (size_t i = 0; i < count; i++)
        if (g_rmask[i]) pixels[i] = g_rrgb[i];
}

/* The wide picture composed with this picture, in the texture; 0 when there is none (the window is not wide, or nothing was composed). */
static int wide_make(SDL_Renderer *ren, int aspect)
{
    (void)ren; (void)aspect;
    if (!g_wide_filled || !g_wtex || !g_wpix) return 0;
    SDL_UpdateTexture(g_wtex, NULL, g_wpix, g_ww * 4);
    return 1;
}

/* The AdLib sidecar (--adlib-effects, sidecar.h): a second driver holding the effects beside Roland's music. Its OPL
 * writes go to a chip of their own (g_side_audio) at the main machine's clock, never earlier than the last write. */
static sidecar_t *g_side;
static int g_hook_flight;               /* the loaded program is the flight program: its sound 31 is the bay doors, not START's launch animation */
static int g_adlib_effects = -1;      /* --adlib-effects: the AdLib driver in a sidecar plays the effects (Roland: music); unset: on with --sound roland */
static audio_t *g_side_audio;
static uint64_t g_side_now, g_side_last, g_side_ticks, g_side_writes, g_side_requests, g_side_failed;
static void side_opl(void *user, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)user; (void)icount;
    g_side_writes++;
    if (g_side_now < g_side_last) g_side_now = g_side_last;
    audio_opl_write(g_side_audio, g_side_now, reg, val);
    g_side_last = g_side_now;
}
static void side_request(const machine_t *m, uint16_t id)
{
    g_side_now = m->cpu.icount;
    g_side_requests++;
    if (!sidecar_request(g_side, id)) g_side_failed++;
}
/* The game's timer ticks at 60.001 Hz of the machine's clock: run the sidecar's ticks that have come due. */
static void side_run(const machine_t *m, uint64_t ips)
{
    if (!g_side) return;
    const uint64_t due = (uint64_t)((double)m->cpu.icount * 60.001 / (double)ips);
    while (g_side_ticks < due) {
        g_side_now = (uint64_t)((double)(g_side_ticks + 1) * (double)ips / 60.001);
        sidecar_tick(g_side);
        g_side_ticks++;
    }
}

/* The rerun copies' collector (rerun_set_collector): only Remastered's entries, into the copy's own instance. A copy has no sound hook. */
static void copy_begin(void *user) { (void)user; if (g_rlive_copy) remaster_live_copy_begin(g_rlive_copy); }
static int copy_exec(void *user, machine_t *m, unsigned index)
{
    (void)user;
    if (!g_rlive_copy || index < g_rlive_base) return 0;
    return remaster_live_exec(g_rlive_copy, m, index - g_rlive_base);
}

static int on_exec_hook(void *user, machine_t *m, unsigned index)
{
    (void)user;
    if (g_rlive && index >= g_rlive_base) return remaster_live_exec(g_rlive, m, index - g_rlive_base);
    if (index != 0) return 0;                                   /* index 0 is the sound stub; the others are Remastered's */
    const uint16_t id = (uint16_t)(m->mem[(((uint32_t)m->cpu.seg[S_SS] << 4) + (uint16_t)(m->cpu.r[R_SP] + 4)) & 0xFFFFF] |
                                   m->mem[(((uint32_t)m->cpu.seg[S_SS] << 4) + (uint16_t)(m->cpu.r[R_SP] + 5)) & 0xFFFFF] << 8);
    static int log_requests = -1;
    if (log_requests < 0) log_requests = getenv("F117_SOUND_LOG") != NULL;   /* diagnostic: every request */
    if (log_requests) fprintf(stderr, "[sound] id %u%s at clock %llu\n", (unsigned)id, g_mac && macsound_has(g_mac, id) ? " (has recording)" : "", (unsigned long long)m->cpu.icount);
    if (g_subtitles && id >= 41 && id <= 45) {                    /* the radio call as text, named by the request itself (every driver) */
        int words[3];
        const int n = macsound_speech_words(id, m->mem[0x46C], words);
        /* 43 is good or nice landing by a tick counter in the driver; a recording the host plays uses its own, the drivers' own are
         * heard by the detector (Roland, PC speaker), so only the Mac's choice is named here */
        if (n && (id != 43 || (g_mac && macsound_has(g_mac, id) && sp_choose(SP_SPEECH, SP_BIT(SP_MAC) | SP_BIT(SP_SPEAKER), g_source[SP_SPEECH]) == SP_MAC)))
            sub_call(&g_sub, words, n, (unsigned long)SDL_GetTicks());
    }
    if (id == 0 && g_mac) macsound_stop_all(g_mac);              /* stop: the DOS drivers silence everything, so do the recordings (a skipped cutscene's music) */
    const uint16_t mid = id == 31 && g_hook_flight ? 331 : id;     /* the Mac id of the bay-door sound (see macsound_resource) */
    const int has_mac = g_mac && macsound_has(g_mac, mid);
    if (!has_mac && !g_side) return 0;
    const sp_kind kind = id == 3 || (id >= 36 && id <= 39) ? SP_MUSIC : id >= 41 && id <= 45 ? SP_SPEECH : SP_SOUND;
    /* Whichever DOS driver is loaded; with the AdLib sidecar (--adlib-effects) the loaded driver is Roland's and AdLib
     * is the sidecar's, so an effect the policy gives to AdLib goes there and music stays with Roland. */
    const unsigned dos_any = SP_BIT(SP_ROLAND) | SP_BIT(SP_ADLIB) | SP_BIT(SP_SPEAKER);
    const sp_source use = sp_choose(kind, (has_mac ? SP_BIT(SP_MAC) : 0) | dos_any, g_source[kind]);
    /* A request the host takes answers as the driver does: the driver's entry 1 returns its handler's result in AX, and the one
     * caller that reads it is the busy-poll (id 2: START, END and PLAYER loop `or ax, ax; jne` until the driver says 0), which is
     * never taken here. Every other call site ignores AX (checked over all of them, docs/sound-code-proof.md), so 0 is right. */
    if (use == SP_MAC) { const int played = macsound_play_tick(g_mac, mid, m->mem[0x46C]); if (played) m->cpu.r[R_AX] = 0; return played; }
    /* The hybrid rule (owner): sound effects are AdLib's, music is Roland's. Ids 4 to 35 are the effects (flight, the front end's picks
     * and marks, the weapons page); id 0 (stop) reaches both drivers; the busy-poll (2), the music (3, 36 to 39), the release (40) and
     * the speech (41 to 45) stay with the driver that is loaded. */
    if (g_side && id == 0) side_request(m, 0);
    if (use == SP_ADLIB && g_side && id >= 4 && id <= 35) {
        side_request(m, id);
        m->cpu.r[R_AX] = 0;
        return 1;
    }
    return 0;
}
static sp_source parse_source_option(const char *flag, const char *v)
{
    if (!strcmp(v, "dos")) return SP_ADLIB;          /* any DOS driver: the Mac recording is not taken */
    const int s = sp_parse_source(v);
    if (s < 0 || s == SP_ROLAND || s == SP_ADLIB || s == SP_SPEAKER) {
        fprintf(stderr, "%s takes default, mac or dos\n", flag);
        exit(2);
    }
    return (sp_source)s;
}
static int g_sub_p61;
static int g_crt, g_crt_mask, g_crt_glow;   /* --crt, --crt-mask, --crt-glow: scanlines, the phosphor mask, the glow on the finished picture (crt.h) */
static int g_colourblind;             /* --colourblind: the colour-blind filter on the finished picture (colourblind.h) */
/* controller buttons acting as keys (--pad-keys, controls.h): the button, the key it presses, and whether it is held now */
#define PAD_KEYS_MAX 16
static struct { int button; SDL_Scancode key; int held; } g_padkeys[PAD_KEYS_MAX];
static int g_npadkeys;
static controls_config g_pad;         /* controller mapping (controls.h), from the INI file */
static uint32_t *g_crt_buf; static size_t g_crt_cap;

/* The picture as the display should get it: with --crt, the scanline filter, and with --colourblind the colour filter, applied to a copy. */
static const uint32_t *crt_view(const uint32_t *px, int w, int h)
{
    if (!g_crt && !g_crt_mask && !g_crt_glow && !g_colourblind) return px;
    if ((size_t)w * h > g_crt_cap) {
        free(g_crt_buf);
        g_crt_buf = (uint32_t *)malloc((size_t)w * h * 4);
        g_crt_cap = g_crt_buf ? (size_t)w * h : 0;
    }
    if (!g_crt_buf) return px;
    const uint32_t *cur = px;
    if (g_crt_glow) { crt_glow(cur, g_crt_buf, w, h); cur = g_crt_buf; }
    if (g_crt) { crt_scanlines(cur, g_crt_buf, w, h); cur = g_crt_buf; }
    if (g_crt_mask) { crt_mask(cur, g_crt_buf, w, h); cur = g_crt_buf; }
    if (g_colourblind) colourblind_apply(cur, g_crt_buf, w, h, g_colourblind);
    return g_crt_buf;
}
static const uint8_t *g_fine;         /* the finer picture to show, or NULL */
static FILE *g_present_log;           /* --present-log */

/* With --present interp the in-between picture is drawn one host frame ahead
 * on a worker thread (planned for the clock a frame on), while the main thread
 * runs the machine, collects the replay and shows the picture drawn last
 * frame. The worker only reads the published steps; the main thread publishes
 * new ones only while the worker is idle (docs/presentation.md). */
typedef struct {
    SDL_Thread *thread;
    SDL_Semaphore *go, *done;
    int busy, quit;
    drawlive_plan plan;
    present_frame frame;
    int got;
} present_worker;
static present_worker g_pw;
static uint64_t g_frame_ips;          /* guest clocks in one host frame */

/* Distinct palettes, presented and scanned out, for the summary: a presentation that freezes the palette
 * shows one against the machine's many (the colour cycling of the gun rounds, fades). */
#define PAL_SET_MAX 1024
typedef struct { uint64_t h[PAL_SET_MAX]; int n; } pal_set;
static pal_set g_pal_shown, g_pal_scanned;
static void pal_note(pal_set *s, const uint8_t *dac)
{
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 768; i++) h = (h ^ dac[i]) * 1099511628211ull;
    for (int k = 0; k < s->n; k++) if (s->h[k] == h) return;
    if (s->n < PAL_SET_MAX) s->h[s->n++] = h;
}

static int present_worker_main(void *u)
{
    present_worker *w = (present_worker *)u;
    for (;;) {
        SDL_WaitSemaphore(w->go);
        if (w->quit) break;
        w->got = drawlive_render(&g_live, &w->plan, &w->frame);
        SDL_SignalSemaphore(w->done);
    }
    return 0;
}

#define PAL_HELPERS 3
typedef struct {
    SDL_Thread *thread;
    SDL_Semaphore *go, *done;
    const uint32_t *pal;
    const uint8_t *src;
    uint32_t *dst;
    size_t from, to;
    int quit;
} pal_helper;
static pal_helper g_pal[PAL_HELPERS];

static int pal_helper_main(void *u)
{
    pal_helper *p = (pal_helper *)u;
    for (;;) {
        SDL_WaitSemaphore(p->go);
        if (p->quit) break;
        for (size_t k = p->from; k < p->to; k++) p->dst[k] = p->pal[p->src[k]];
        SDL_SignalSemaphore(p->done);
    }
    return 0;
}

/* Helpers for the Remastered composite: a picture's rows are shared among them and the calling thread (remaster_live_set_parallel). */
#define PAR_HELPERS 3
typedef struct {
    SDL_Thread *thread;
    SDL_Semaphore *go, *done;
    void (*fn)(void *, int);
    void *arg;
    int part, quit;
} par_helper;
static par_helper g_par[PAR_HELPERS];

static int par_helper_main(void *u)
{
    par_helper *p = (par_helper *)u;
    for (;;) {
        SDL_WaitSemaphore(p->go);
        if (p->quit) break;
        p->fn(p->arg, p->part);
        SDL_SignalSemaphore(p->done);
    }
    return 0;
}

static void par_run(void (*fn)(void *, int), void *arg, int parts)
{
    int used = 0;
    for (int h = 0; h < PAR_HELPERS && h + 1 < parts; h++) {
        par_helper *p = &g_par[h];
        if (!p->thread) { fn(arg, h + 1); continue; }          /* no helper: this share is done here */
        p->fn = fn; p->arg = arg; p->part = h + 1;
        SDL_SignalSemaphore(p->go);
        used |= 1 << h;
    }
    fn(arg, 0);
    for (int h = 0; h < PAR_HELPERS; h++)
        if (used & (1 << h)) SDL_WaitSemaphore(g_par[h].done);
}

/* The fine picture through the frame's palette, in bands on the helpers. */
static void convert_fine(const uint8_t *dac, const uint8_t *src, uint32_t *dst, size_t n)
{
    uint32_t pal[256];
    for (int k = 0; k < 256; k++) {
        const uint8_t *c = &dac[3 * k];
        pal[k] = 0xFF000000u | (uint32_t)((c[0] << 2) | (c[0] >> 4)) << 16 |
                 (uint32_t)((c[1] << 2) | (c[1] >> 4)) << 8 | (uint32_t)((c[2] << 2) | (c[2] >> 4));
    }
    const size_t band = n / (PAL_HELPERS + 1);
    for (int h = 0; h < PAL_HELPERS; h++) {
        pal_helper *p = &g_pal[h];
        const size_t from = band * (size_t)h, to = band * (size_t)(h + 1);
        if (!p->thread) {                                    /* no helper: this band is done here */
            for (size_t k = from; k < to; k++) dst[k] = pal[src[k]];
            continue;
        }
        p->pal = pal; p->src = src; p->dst = dst; p->from = from; p->to = to;
        SDL_SignalSemaphore(p->go);
    }
    for (size_t k = band * PAL_HELPERS; k < n; k++) dst[k] = pal[src[k]];
    for (int h = 0; h < PAL_HELPERS; h++)
        if (g_pal[h].thread) SDL_WaitSemaphore(g_pal[h].done);
}

/* ---- machine hooks ------------------------------------------------------ */

static void on_opl(void *u, uint64_t icount, uint8_t reg, uint8_t val)
{
    (void)u;
    const uint64_t t0 = g_loop_timing ? SDL_GetPerformanceCounter() : 0;
    if (H.audio) audio_opl_write(H.audio, icount, reg, val);
    if (g_loop_timing) { g_lt_hook[0] += SDL_GetPerformanceCounter() - t0; g_lt_calls[0]++; }
}

static void on_speaker(void *u, uint64_t icount)
{
    (void)u;
    const uint64_t t0 = g_loop_timing ? SDL_GetPerformanceCounter() : 0;
    if (g_loop_timing) g_lt_calls[1]++;
    if (H.audio) audio_speaker(H.audio, &H.m, icount);
    if (g_loop_timing) g_lt_hook[1] += SDL_GetPerformanceCounter() - t0;
    if (g_sub.ready) {          /* the radio calls as text: the counts written to PIT counter 2 in mode 0 */
        const uint8_t p61 = H.m.port61 & 3;
        if (p61 != g_sub_p61) g_sub_p61 = p61;
        else if (!H.m.pit[2].null_count && H.m.pit[2].mode == 0 && p61 == 3)
            sub_count(&g_sub, H.m.pit[2].reload, (unsigned long)SDL_GetTicks());
    }
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
    const uint64_t t0 = g_loop_timing ? SDL_GetPerformanceCounter() : 0;
    if (g_loop_timing) g_lt_calls[2]++;
    present_capture(&H.m, &H.frame);
    if (g_loop_timing && g_replay != 1) g_lt_hook[2] += SDL_GetPerformanceCounter() - t0;
    if (g_replay == 1) {                  /* interp is drawn when the host presents */
        drawlive_update(&g_live, &g_feed);
        g_presented += (uint64_t)(g_replay == 2 ? drawlive_present_interp(&g_live, &H.m, &H.frame)
                                                : drawlive_present(&g_live, &H.m, &H.frame));
        g_fine = remaster_apply(g_live.shown_hi);
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
    {   /* F117_MIDI_LOG=FILE (diagnostic): every byte the game sends to the MPU data port, with the machine clock */
        static FILE *mlog; static int tried;
        if (!tried) { tried = 1; const char *path = getenv("F117_MIDI_LOG"); if (path) mlog = fopen(path, "w"); }
        if (mlog) fprintf(mlog, "%llu %02X\n", (unsigned long long)icount, (unsigned)b);
    }
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

/* The controller-key list as f117a.ini holds it ("2:G,3:Left Shift"), written from the table; NULL clears the key. */
static void padkeys_save(const char *settings_path)
{
    char list[PAD_KEYS_MAX * (CONTROLS_KEYNAME + 6)] = "";
    for (int j = 0; j < g_npadkeys; j++) {
        const size_t used = strlen(list);
        snprintf(list + used, sizeof list - used, "%s%d:%s", used ? "," : "", g_padkeys[j].button, SDL_GetScancodeName(g_padkeys[j].key));
    }
    char *text = config_read_file(settings_path);
    char *out = config_set(text ? text : "", "pad-keys", list[0] ? list : NULL);
    FILE *f = out ? fopen(settings_path, "wb") : NULL;
    if (f) { fputs(out, f); fclose(f); }
    free(text);
    free(out);
}

/* a controller button now acts as a key (a button already mapped is changed) */
static void padkeys_set(int button, SDL_Scancode key)
{
    int j = 0;
    while (j < g_npadkeys && g_padkeys[j].button != button) j++;
    if (j == g_npadkeys) { if (g_npadkeys == PAD_KEYS_MAX) return; g_npadkeys++; }
    g_padkeys[j].button = button; g_padkeys[j].key = key; g_padkeys[j].held = 0;
}

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
    if (g_wide_shown && g_ww > 0 && g_wh > 0) {          /* the wide Remastered view: the game's picture sits in the middle at its own size */
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        const float n = (float)g_wh / 200.0f, tx = ow > 0 ? rx / (float)ow * (float)g_ww : 0.0f, ty = oh > 0 ? ry / (float)oh * 200.0f : 0.0f;
        float gxf = (tx - (float)(g_ww - 320 * (int)n) / 2.0f) / n;
        *gx = gxf < 0 ? 0 : gxf > 319.0f ? 319 : (int)gxf;
        *gy = ty < 0 ? 0 : ty > 199.0f ? 199 : (int)ty;
        return;
    }
    float u = (rx - r.x) / r.w, v = (ry - r.y) / r.h;
    if (u < 0) u = 0;
    if (u > 0.9999f) u = 0.9999f;
    if (v < 0) v = 0;
    if (v > 0.9999f) v = 0.9999f;
    *gx = (int)(u * 320.0f);
    *gy = (int)(v * 200.0f);
}

/* The other way: the window position of a point of the game's 320 x 200 picture (its middle). */
static void guest_to_window(SDL_Renderer *ren, int aspect, int gx, int gy, float *wx, float *wy)
{
    float rx, ry;
    if (g_wide_shown && g_ww > 0 && g_wh > 0) {
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(ren, &ow, &oh);
        const float n = (float)g_wh / 200.0f;
        const float tx = (float)(g_ww - 320 * (int)n) / 2.0f + ((float)gx + 0.5f) * n;
        rx = tx / (float)g_ww * (float)ow;
        ry = ((float)gy + 0.5f) / 200.0f * (float)oh;
    } else {
        const SDL_FRect r = output_rect(ren, aspect);
        rx = r.x + ((float)gx + 0.5f) / 320.0f * r.w;
        ry = r.y + ((float)gy + 0.5f) / 200.0f * r.h;
    }
    SDL_RenderCoordinatesToWindow(ren, rx, ry, wx, wy);
}

/* The DOS mouse driver is relative: when the game puts its cursor somewhere (the driver's "set position" call, as when it jumps to
 * a button), the next movement continues from there. Here the host sends absolute positions, so the game's cursor used to jump
 * back to the (hidden) Windows cursor. Whenever the game moves its cursor with no mouse input of ours pending, the Windows cursor is
 * moved to the same place, so the next real movement starts from the game's cursor. Not in a replay (its inputs come from the log). */
static int g_mouse_dirty;               /* a mouse event of ours is on its way into the machine */
static int g_mouse_seen_x = -1, g_mouse_seen_y = -1;
static int g_mouse_sent_x = -1, g_mouse_sent_y = -1;   /* what our last mouse event put in the driver (its coordinates) */
static void mouse_follow_game(SDL_Window *win, SDL_Renderer *ren, int aspect, int skip)
{
    if (H.m.in_qn) return;                                 /* an input is still waiting: the driver's position is not settled */
    const int mx = H.m.mouse_x, my = H.m.mouse_y;
    if (g_mouse_dirty) {                                   /* ours: accepted, unless the game moved the cursor as well (a click that jumps it) */
        g_mouse_dirty = 0; g_mouse_seen_x = mx; g_mouse_seen_y = my;
        if (mx == g_mouse_sent_x && my == g_mouse_sent_y) return;
    } else if (mx == g_mouse_seen_x && my == g_mouse_seen_y) return;
    g_mouse_seen_x = mx; g_mouse_seen_y = my;              /* the game moved it */
    if (skip || SDL_GetMouseFocus() != win) return;               /* a replay or the settings menu: the cursor is not ours to move */
    float wx = 0, wy = 0;
    guest_to_window(ren, aspect, mx / 2, my, &wx, &wy);
    SDL_WarpMouseInWindow(win, wx, wy);
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

/* Saves carry the fix set that last wrote them (docs/roadmap.md, Phase 3), in a file beside the save
 * folder, never in it: the game sees only its own files. Crossing between the original game (every fix
 * off) and a fixed set warns once, since a saved state one setting produces may be one the other never
 * could; other changes of the set pass silently. */
static void save_fixset_check(const char *save, SDL_Window *win)
{
    char path[760], cur[512], prev[512] = { 0 };
    size_t len = strlen(save);
    while (len && (save[len - 1] == '/' || save[len - 1] == '\\')) len--;   /* beside the folder, not in it */
    snprintf(path, sizeof path, "%.*s.fixset", (int)len, save);
    fixes_enabled(cur, sizeof cur);
    FILE *f = fopen(path, "rb");
    if (f) {
        const size_t n = fread(prev, 1, sizeof prev - 1, f);
        prev[n] = 0;
        prev[strcspn(prev, "\r\n")] = 0;
        fclose(f);
        if (!prev[0] != !cur[0]) {
            char text[400];
            snprintf(text, sizeof text,
                     "These saves were last played %s, and this session plays %s.\n\n"
                     "A pilot's saved state from one may be one the other could never produce.",
                     prev[0] ? "with fixes" : "as the original game, with every fix off",
                     cur[0] ? "with fixes" : "as the original game, with every fix off");
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "F-117A", text, win);
        }
    }
    f = fopen(path, "wb");
    if (f) { fprintf(f, "%s\n", cur); fclose(f); }
}

/* The settings menu saved a setting that is read when the game starts: offer to start it again. */
static int restart_wanted(SDL_Window *win)
{
    static const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Restart now" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Later" },
    };
    const SDL_MessageBoxData box = { SDL_MESSAGEBOX_INFORMATION, win, "F-117A",
        "Saved. Some of these settings apply when the game next starts.\n\nRestart now? The mission in progress is not saved.",
        2, buttons, NULL };
    int pressed = 0;
    return SDL_ShowMessageBox(&box, &pressed) && pressed == 1;
}

/* Start the game again with the arguments it was given (each quoted: the C runtime joins them into one
 * command line, so a path with a space would otherwise split). The caller then quits. */
static void restart_game(char **orig_argv, const char *extra1, const char *extra2)
{
    if (getenv("F117_RESTART_DRYRUN")) {            /* diagnostic: say what would be started, start nothing */
        for (int i = 0; orig_argv[i]; i++) fprintf(stderr, "%s ", orig_argv[i]);
        fprintf(stderr, "%s %s\n", extra1 ? extra1 : "", extra2 ? extra2 : "");
        return;
    }
#ifdef _WIN32
    int n = 0;
    while (orig_argv[n]) n++;
    char **q = (char **)calloc((size_t)n + 3, sizeof *q);
    if (!q) return;
    for (int i = 0; i < n; i++) {
        const size_t len = strlen(orig_argv[i]) + 3;
        q[i] = (char *)malloc(len);
        if (!q[i]) return;
        snprintf(q[i], len, "\"%s\"", orig_argv[i]);
    }
    int m = n;
    if (extra1) { q[m] = (char *)malloc(strlen(extra1) + 3); if (q[m]) snprintf(q[m], strlen(extra1) + 3, "\"%s\"", extra1); m++; }
    if (extra2) { q[m] = (char *)malloc(strlen(extra2) + 3); if (q[m]) snprintf(q[m], strlen(extra2) + 3, "\"%s\"", extra2); m++; }
    _spawnvp(_P_NOWAIT, orig_argv[0], (const char *const *)q);
#else
    (void)orig_argv; (void)extra1; (void)extra2;
#endif
}

/* --sound adlib|roland|speaker: answer SETUP's two questions ourselves (is there a joystick: no; which sound driver),
 * as the gate's routes do: keys typed at fixed instruction counts after SETUP starts. Never during a replay, which
 * brings its own inputs; the typed keys are recorded like any other input. */
static char g_setup_sound;                           /* the digit SETUP wants: '1' speaker, '2' AdLib, '3' Roland; 0 = ask */

static void setup_key(machine_t *m, uint64_t at, char key)
{
    char text[2] = { key, 0 };
    const char *k = text;
    uint8_t mk[4], br[4];
    int nm, nb;
    if (!keys_next(&k, mk, &nm, br, &nb)) return;
    const uint64_t hold = 540000;                    /* as 60 ms at the gate's 9 million a second */
    for (int i = 0; i < nm; i++) { machine_input in = { 0 }; in.at = at; in.type = INPUT_KEY; in.byte = mk[i]; machine_input_at(m, &in); }
    for (int i = 0; i < nb; i++) { machine_input in = { 0 }; in.at = at + hold; in.type = INPUT_KEY; in.byte = br[i]; machine_input_at(m, &in); }
}

static void app_module_load(void *user, machine_t *m, const char *name, const uint8_t *file, size_t len, int kind,
                            uint16_t load_seg, uint16_t reloc)
{
    recomp_module_load(user, m, name, file, len, kind, load_seg, reloc);
    if (kind != MODLOAD_OVERLAY) {
        m->exec_hook_n = 0;
        g_hook_flight = !SDL_strcasecmp(name, "VGAME.EXE");
        if (g_mac || g_side || g_subtitles) {                                  /* each program's sound stub: a far jump slot the driver patches */
            static const struct { const char *name; uint16_t seg, ip; } slot[] = {
                { "VGAME.EXE", 0x1E42, 0x0313 }, { "START.EXE", 0x0A95, 0x6BB1 }, { "END.EXE", 0x05FA, 0x25F3 },
                { "PLAYER.EXE", 0x02A3, 0x13B9 },       /* the intro: its music is id 3 */
            };
            for (unsigned k = 0; k < sizeof slot / sizeof slot[0]; k++)
                if (!SDL_strcasecmp(name, slot[k].name)) {
                    m->exec_hook[0].cs = (uint16_t)(load_seg + slot[k].seg);
                    m->exec_hook[0].ip = slot[k].ip;
                    m->exec_hook_n = 1;
                }
        }
        if (!SDL_strcasecmp(name, "VGAME.EXE")) {
            const char *spec = getenv("F117_REMASTER_FRAME");       /* N:FILE.ppm: write the Nth frame's geometry as a picture */
            if (g_remaster && !g_rlive) g_rlive = remaster_live_new(960, 720, ~0u, NULL);
            if (spec && !g_rlive) {
                const unsigned frame = (unsigned)strtoul(spec, NULL, 10);
                const char *colon = strchr(spec, ':');
                g_rlive = remaster_live_new(960, 720, frame, colon ? colon + 1 : NULL);
            }
            if (g_rlive) g_rlive_base = remaster_live_arm(g_rlive, m, load_seg);
            if (g_rlive && !g_rlive_copy) {                 /* the copies' collector: the same watched entries (the snapshot keeps the table) */
                g_rlive_copy = remaster_live_new(960, 720, ~0u, NULL);
                static const rerun_collector coll = { copy_begin, copy_exec, NULL, NULL };
                if (g_rlive_copy) rerun_set_collector(&coll);
            }
        }
    }
    if (kind != MODLOAD_OVERLAY && g_setup_sound && !SDL_strcasecmp(name, "SETUP.EXE")) {
        setup_key(m, m->cpu.icount + 200000, 'N');
        setup_key(m, m->cpu.icount + 2000000, g_setup_sound);
    }
}

static int newest_first(const void *a, const void *b)
{
    return strcmp(*(char *const *)b, *(char *const *)a);
}

/* Watch a flight: the recorded sessions (sessions/session-<time>/ beside the save folder) that have an input log,
 * newest first, each shown as its date and the log's size. */
static void fill_flight_list(settings_state *st, const char *save_dir)
{
    char sessions[900];
    snprintf(sessions, sizeof sessions, "%s/../sessions", save_dir);
    int n = 0;
    char **names = SDL_GlobDirectory(sessions, "session-*", 0, &n);
    if (!names) return;
    qsort(names, (size_t)n, sizeof *names, newest_first);
    int listed = 0;
    for (int k = 0; k < n && listed < 16; k++) {
        char log[1000];
        snprintf(log, sizeof log, "%s/%s/input.log", sessions, names[k]);
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(log, &info) || info.type != SDL_PATHTYPE_FILE) continue;
        unsigned y, mo, d, h, mi, se;
        char what[64];
        if (sscanf(names[k], "session-%4u%2u%2u-%2u%2u%2u", &y, &mo, &d, &h, &mi, &se) == 6)
            snprintf(what, sizeof what, "%04u-%02u-%02u %02u:%02u:%02u  %llu KB", y, mo, d, h, mi, se,
                     (unsigned long long)((info.size + 1023) / 1024));
        else snprintf(what, sizeof what, "%s", names[k]);
        settings_set_flight(st, listed++, names[k], what);
    }
    SDL_free(names);
}

/* The subtitle line over the picture, in the font doubled, on a dark band across the lower part of the frame. */
static void draw_subtitle(uint32_t *px, const char *line)
{
    const int len = (int)strlen(line), w = len * 16, x0 = (640 - w) / 2, y0 = 336, pad = 8;
    for (int y = y0 - pad; y < y0 + 32 + pad; y++)
        for (int x = x0 - 2 * pad; x < x0 + w + 2 * pad; x++)
            if (x >= 0 && x < 640 && y >= 0 && y < 400) px[y * 640 + x] = 0xC0000000u;
    for (int i = 0; i < len; i++) {
        const unsigned char ch = (unsigned char)line[i];
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 8; c++)
                if (FONT8X16[ch][r] & (0x80 >> c))
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            const int x = x0 + i * 16 + c * 2 + dx, y = y0 + r * 2 + dy;
                            if (x >= 0 && x < 640 && y >= 0 && y < 400) px[y * 640 + x] = 0xFFFFFFFFu;
                        }
    }
}

/* The settings menu's fix picker: every fix with what it does and whether it is on in this run (fixes_list). */
static void fill_fix_picker(settings_state *st)
{
    FILE *f = tmpfile();
    if (!f) return;
    fixes_list(f);
    rewind(f);
    char line[512];
    for (int i = 0; fgets(line, sizeof line, f) && i < SETTINGS_MAX_FIXES; i++) {
        char id[16] = "", state[8] = "";
        int used = 0;
        if (sscanf(line, "%15s %7s %n", id, state, &used) < 2) { i--; continue; }
        char *what = line + used;
        what[strcspn(what, "\r\n")] = 0;
        settings_set_fix(st, i, id, what, !strcmp(state, "on"));
    }
    fclose(f);
}

/* The picker closed with a change: write fixes = custom and the list of fixes that are on to the INI file. */
static void write_custom_fixes(settings_state *st, const char *path)
{
    char list[600] = "";
    for (int i = 0; i < st->pick_count; i++)
        if (st->pick_on[i]) {
            const size_t used = strlen(list);
            snprintf(list + used, sizeof list - used, "%s%s", used ? ", " : "", st->pick_id[i]);
        }
    char *text = config_read_file(path);
    char *a = config_set(text ? text : "", "fixes", "custom");
    char *b = a ? config_set(a, "fix", list[0] ? list : "none") : NULL;
    FILE *f = b ? fopen(path, "wb") : NULL;
    if (f) {
        fputs(b, f);
        fclose(f);
        settings_mark_saved(st, "fixes", "custom");
        snprintf(st->note, sizeof st->note, "Custom fixes saved; applies after restart");
    } else snprintf(st->note, sizeof st->note, "Could not write f117a.ini");
    free(text);
    free(a);
    free(b);
}

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *log_path = NULL;
    const char *audio_queue_log_path = NULL;
    const char *audio_dump_path = NULL;
    uint64_t ips = MACHINE_APP_IPS;
    unsigned sim_hz = 15;
    int scale = 3, fullscreen = 0, aspect = 1, midi_dev = -2;
    int engine = ENGINE_RECOMP;
    const char *coverage = NULL, *record = NULL, *replay = NULL, *watch = NULL;
    double seek_secs = 0;
    uint64_t time_us = 0, exit_after = 0;
    audio_opl_backend opl_backend = AUDIO_OPL_DBOPL;
    int speaker_model = 0;                  /* speaker.h: realsound */
    int no_record = 0;
    const char *mt32_control = NULL, *mt32_pcm = NULL;
    unsigned int mt32_seed = 0;
    int mt32_seed_set = 0;

    /* The settings menu (Ctrl+Alt+S) writes the INI file the run reads, and restarts with the arguments
     * as typed; --no-config leaves it without a file, so the menu is not offered. */
    char **orig_argv = argv;
    char settings_path[1024] = "";

    /* f117a.ini (beside the executable, or --config FILE): its options go
     * ahead of the command line's, so the command line wins. */
    {
        int explicit_ = 0;
        const char *ini = config_path(argc, argv, SDL_GetBasePath(), &explicit_);
        {
            int no_config = 0;
            for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--no-config")) no_config = 1;
            if (!no_config) {
                const char *base = SDL_GetBasePath();
                snprintf(settings_path, sizeof settings_path, "%s%s", ini ? "" : base ? base : "", ini ? ini : "f117a.ini");
            }
        }
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
    /* Normal play enables all fixes, including the timing/balance options.
     * Recorded sessions keep their
     * recorded fix set (old logs without a fix header mean the original).
     * Explicit --fix options can still add fixes to a diagnostic replay. */
    int replay_requested = 0, fixes_custom = 0, cli_fix_flags = 0, replay_fixes_cli = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--replay")) replay_requested = 1;
        if (!strcmp(argv[i], "--fix") || !strcmp(argv[i], "--fixes")) cli_fix_flags = 1;
        /* --replay-fixes cli: run the replay with the fixes the command line names, not the ones its log header records */
        if (!strcmp(argv[i], "--replay-fixes") && i + 1 < argc && !strcmp(argv[i + 1], "cli")) replay_fixes_cli = 1;
        /* --fixes custom (a Custom fix set): start from none, so the --fix IDs given add to nothing, wherever they
         * come relative to it in the file */
        if (!strcmp(argv[i], "--fixes") && i + 1 < argc && !strcmp(argv[i + 1], "custom")) fixes_custom = 1;
    }
    if (!replay_requested)
        fixes_enable("all", fixes_custom ? 0 : 1);

    controls_defaults(&g_pad);
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
        else if (!strcmp(a, "--crt")) g_crt = 1;
        else if (!strcmp(a, "--crt-mask")) g_crt_mask = 1;
        else if (!strcmp(a, "--crt-glow")) g_crt_glow = 1;
        else if (!strcmp(a, "--colourblind") && v) {
            g_colourblind = colourblind_parse(v);
            if (g_colourblind < 0) { fprintf(stderr, "--colourblind takes off, protanopia, deuteranopia or tritanopia\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--sound") && v) {
            if (!strcmp(v, "speaker")) g_setup_sound = '1';
            else if (!strcmp(v, "adlib")) g_setup_sound = '2';
            else if (!strcmp(v, "roland")) g_setup_sound = '3';
            else if (!strcmp(v, "ask")) g_setup_sound = 0;
            else { fprintf(stderr, "--sound takes ask, adlib, roland or speaker\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--subtitles")) g_subtitles = 1;
        else if (!strcmp(a, "--adlib-effects")) g_adlib_effects = 1;
        else if (!strcmp(a, "--no-adlib-effects")) g_adlib_effects = 0;
        else if (!strcmp(a, "--mac-sounds") && v) { g_mac = macsound_load(v, AUDIO_RATE); if (!g_mac) fprintf(stderr, "--mac-sounds: no NNN.wav recordings in %s\n", v); i++; }
        else if (!strcmp(a, "--music-source") && v) { g_source[SP_MUSIC] = parse_source_option(a, v); i++; }
        else if (!strcmp(a, "--sound-source") && v) { g_source[SP_SOUND] = parse_source_option(a, v); i++; }
        else if (!strcmp(a, "--remap") && v) {
            if (remap_add(v) < 0) { fprintf(stderr, "--remap takes pairs of key names, e.g. Q:W (SDL names: A, Left Shift, F5, Keypad 4)\n"); return 2; }
            i++;
        }
        else if (!strncmp(a, "--pad-", 6) && v) {          /* controller mapping (controls.h) */
            const long n = strtol(v, NULL, 10);
            const int on = v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T' || n == 1;
            if (!strcmp(a, "--pad-deadzone")) { if (n < 0 || n > 50) { fprintf(stderr, "--pad-deadzone takes 0 to 50\n"); return 2; } g_pad.deadzone = (int)n; }
            else if (!strcmp(a, "--pad-fire1") || !strcmp(a, "--pad-fire2")) {
                if (n < 0 || n > 31) { fprintf(stderr, "%s takes a button number 0 to 31\n", a); return 2; }
                if (!strcmp(a, "--pad-fire1")) g_pad.fire1 = (int)n; else g_pad.fire2 = (int)n;
            }
            else if (!strcmp(a, "--pad-keys")) {
                int btn[PAD_KEYS_MAX]; char names[PAD_KEYS_MAX][CONTROLS_KEYNAME];
                const int k = controls_pad_keys(v, btn, names, PAD_KEYS_MAX);
                if (k < 0) { fprintf(stderr, "--pad-keys takes button:key pairs, e.g. 2:G,3:Left Shift (SDL key names)\n"); return 2; }
                g_npadkeys = 0;
                for (int j = 0; j < k; j++) {
                    const SDL_Scancode sc = SDL_GetScancodeFromName(names[j]);
                    if (sc == SDL_SCANCODE_UNKNOWN) { fprintf(stderr, "--pad-keys: no key named %s\n", names[j]); return 2; }
                    g_padkeys[g_npadkeys].button = btn[j]; g_padkeys[g_npadkeys].key = sc; g_padkeys[g_npadkeys].held = 0; g_npadkeys++;
                }
            }
            else if (!strcmp(a, "--pad-invert-x")) g_pad.invert_x = on;
            else if (!strcmp(a, "--pad-invert-y")) g_pad.invert_y = on;
            else { fprintf(stderr, "unknown option %s\n", a); return 2; }
            i++;
        }
        else if (!strcmp(a, "--replay") && v) { replay = v; i++; }
        else if (!strcmp(a, "--watch") && v) { watch = v; i++; }
        else if (!strcmp(a, "--seek") && v) { seek_secs = atof(v); i++; }       /* a replay starts this many seconds in */
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
        else if (!strcmp(a, "--sim-hz") && v) {
            if (strcmp(v, "8") && strcmp(v, "15")) {
                fprintf(stderr, "--sim-hz requires 8 or 15\n"); return 2;
            }
            sim_hz = (unsigned)atoi(v); i++;
        }
        else if (!strcmp(a, "--timing") && v) {
            /* dosbox (the default): this machine's instruction model, run by the app at 60 million
             * instructions a second (--ips); GOG DOSBox ran 9 million (--ips 9000000), at which a
             * parked jet draws 11.6 frames a second (docs/bugs.md D1). 386: the 386DX/33 profile,
             * the pace of a 1991 PC (src/cpu/timing386.h) */
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
            if (!strcmp(v, "none")) fixes_enable("all", 0);
            else if (!fixes_enable(v, 1)) { fprintf(stderr, "no fix %s (--list-fixes)\n", v); return 2; }
            i++;
        }
        else if (!strcmp(a, "--replay-fixes") && v) {
            if (strcmp(v, "log") && strcmp(v, "cli")) { fprintf(stderr, "--replay-fixes takes log or cli\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--list-fixes")) { fixes_list(stdout); return 0; }
        else if (!strcmp(a, "--present") && v) {
            if (strcmp(v, "replay") && strcmp(v, "scan") && strcmp(v, "interp") && strcmp(v, "rerun")) {
                fprintf(stderr, "--present takes scan, replay, interp or rerun\n");
                return 2;
            }
            g_rerun = !strcmp(v, "rerun");
            g_replay = !strcmp(v, "replay") ? 1 : !strcmp(v, "interp") || g_rerun ? 2 : 0;
            i++;
        }
        else if (!strcmp(a, "--present-log") && v) {
            g_present_log = fopen(v, "w");
            if (!g_present_log) { fprintf(stderr, "cannot write %s\n", v); return 2; }
            fprintf(g_present_log, "host_ns icount older newer t replayed\n");
            i++;
        }
        else if (!strcmp(a, "--presentation") && v) {
            /* the re-release's presentation setting (docs/roadmap.md, Phase 3): shorthand for the --present
             * options, which a later one still refines; Remastered needs its own renderer */
            if (!strcmp(v, "original")) { g_replay = g_rerun = g_scale = g_fit_scale = 0; }
            else if (!strcmp(v, "enhanced")) { g_replay = 2; g_rerun = 1; g_fit_scale = 1; }
            else if (!strcmp(v, "remastered")) {                 /* the Enhanced picture with the 3D view redrawn (remaster_live.h) */
                g_replay = 2; g_rerun = 1; g_fit_scale = 1; g_remaster = 1;
#ifdef _WIN32
                _putenv_s("F117_PRESENT_DIAG", "1");              /* the origin planes say which pixels are the 3D view */
#else
                setenv("F117_PRESENT_DIAG", "1", 0);
#endif
            }
            else { fprintf(stderr, "--presentation takes original, enhanced or remastered\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--fixes") && v) {
            /* the re-release's fix setting: original (none) or recommended (the normal-play set); a later
             * --fix ID adds one, or after --fix none picks single fixes (Custom) */
            if (!strcmp(v, "original")) fixes_enable("all", 0);
            else if (!strcmp(v, "recommended")) { fixes_enable("all", 0); fixes_enable("all", 1); }
            else if (!strcmp(v, "custom")) { /* the set the --fix options name, started from none above */ }
            else { fprintf(stderr, "--fixes takes original, recommended or custom\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--present-scale") && v && !strcmp(v, "auto")) i++;      /* the settings menu's choice: as fine as the display allows */
        else if (!strcmp(a, "--present-scale") && v) {
            g_fit_scale = 0;
            g_scale = atoi(v);
            if (g_scale < 1 || g_scale > 16) { fprintf(stderr, "--present-scale takes 1 to 16\n"); return 2; }
            i++;
        }
        else if (!strcmp(a, "--present-line-width") && v) {
            char *end;
            const long width = strtol(v, &end, 10);
            if (!*v || *end || width < 0 || width > 20) { fprintf(stderr, "--present-line-width takes 0 to 20 (0 scales with resolution)\n"); return 2; }
            g_line_width = (int)width;
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
                "             [--sim-hz 8|15] (D1REAL cadence; default 15 at 60000000 ips; replay restores recorded choice)\n"
                "             [--exit-after CLOCKS] [--opl dbopl|nuked] [--speaker realsound|pwm]\n"
                "             [--audio-queue-log FILE]\n"
                "             [--presentation original|enhanced] [--fixes original|recommended|custom] [--sound ask|adlib|roland|speaker] [--watch latest|SESSION] [--seek SECONDS]\n"
                "             [--audio-dump FILE] [--present scan|replay|interp|rerun] [--present-scale N]\n"
                "             [--present-line-width 0..20] [--present-age interp|extrapolate] [--present-log FILE]\n"
                "             [--roland munt|windows|off] [--mt32-roms DIR]\n"
                "             [--mt32-control FILE --mt32-pcm FILE] [--fix ID|all|none]... [--list-fixes]\n"
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
    if (g_subtitles) {                              /* the detector's templates come from the player's own SPEECH.117 */
        char sp[1100];
        snprintf(sp, sizeof sp, "%s/SPEECH.117", data);
        static uint8_t speech_bytes[26408 + 1];
        FILE *sf = fopen(sp, "rb");
        const size_t got = sf ? fread(speech_bytes, 1, sizeof speech_bytes, sf) : 0;
        if (sf) fclose(sf);
        if (!sub_init(&g_sub, speech_bytes, got)) fprintf(stderr, "subtitles: %s is not the game's SPEECH.117; none shown\n", sp);
    }
    char save_buf[700];
    if (!save) {
        const char *base = SDL_GetPrefPath("F117Recomp", "F-117A");
        snprintf(save_buf, sizeof save_buf, "%ssave", base ? base : "./");
        save = save_buf;
    }
    /* --watch latest|SESSION: replay a recorded flight (sessions/session-<time>/ beside the save folder) from
     * a scratch copy of the save it started with, so the player's own saves are never touched. */
    char watch_log[900], watch_save[900];
    if (watch) {
        char sessions[800], name[200] = "";
        snprintf(sessions, sizeof sessions, "%s/../sessions", save);      /* where the recorder writes them */
        if (strcmp(watch, "latest")) snprintf(name, sizeof name, "%s", watch);
        else {
            int n = 0;
            char **names = SDL_GlobDirectory(sessions, "session-*", 0, &n);
            for (int k = 0; names && k < n; k++) {
                char probe[1000];
                snprintf(probe, sizeof probe, "%s/%s/input.log", sessions, names[k]);
                FILE *pf = fopen(probe, "rb");
                if (pf && strcmp(names[k], name) > 0) snprintf(name, sizeof name, "%s", names[k]);
                if (pf) fclose(pf);
            }
            SDL_free(names);
        }
        snprintf(watch_log, sizeof watch_log, "%s/%s/input.log", sessions, name);
        FILE *lf = name[0] ? fopen(watch_log, "rb") : NULL;
        if (!lf) {
            fprintf(stderr, "No recorded flight to watch (%s)\n", name[0] ? watch_log : sessions);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "F-117A", "There is no recorded flight to watch yet.", NULL);
        } else {
            fclose(lf);
            snprintf(watch_save, sizeof watch_save, "%s/../watch-save", save);
            int n = 0;
            char **old = SDL_GlobDirectory(watch_save, NULL, 0, &n);
            for (int k = 0; old && k < n; k++) {
                char p[1000];
                snprintf(p, sizeof p, "%s/%s", watch_save, old[k]);
                SDL_RemovePath(p);
            }
            SDL_free(old);
            SDL_CreateDirectory(watch_save);
            char start[1000];
            snprintf(start, sizeof start, "%s/%s/save-start", sessions, name);
            char **files = SDL_GlobDirectory(start, NULL, 0, &n);
            for (int k = 0; files && k < n; k++) {
                char from[1100], to[1000];
                snprintf(from, sizeof from, "%s/%s", start, files[k]);
                snprintf(to, sizeof to, "%s/%s", watch_save, files[k]);
                SDL_CopyFile(from, to);
            }
            SDL_free(files);
            replay = watch_log;
            save = watch_save;
            fprintf(stderr, "Watching %s\n", name);
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    int win_w = 320 * scale, win_h = 240 * scale;
    {   /* F117_WINDOW=WxH (diagnostic): a window of another shape, for the widescreen Remastered view */
        const char *spec = getenv("F117_WINDOW");
        int ww = 0, wh = 0;
        if (spec && sscanf(spec, "%dx%d", &ww, &wh) == 2 && ww >= 320 && wh >= 240 && ww <= 8192 && wh <= 8192) { win_w = ww; win_h = wh; }
    }
    if (!SDL_CreateWindowAndRenderer("F-117A Nighthawk Stealth Fighter 2.0",
                                     win_w, win_h,
                                     SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0),
                                     &win, &ren)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(ren, 1);
    g_ren = ren; g_aspect_on = aspect;
    if (g_fit_scale) {
        /* Enhanced: as fine as the display's height allows at 4:3 (2160 lines: 9, 1440: 6, 1080: 4) */
        const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(win));
        g_scale = mode ? mode->h / 240 : 4;
        if (g_scale < 1) g_scale = 1;
        if (g_scale > 16) g_scale = 16;
    }
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
    /* the settings menu's overlay: transparent, drawn over the game's picture while the menu is open */
    SDL_Texture *tex_menu = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 400);
    if (tex_menu) {
        SDL_SetTextureScaleMode(tex_menu, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(tex_menu, SDL_BLENDMODE_BLEND);
    }
    static uint32_t menu_pixels[640 * 400];
    settings_state g_set;
    memset(&g_set, 0, sizeof g_set);
    SDL_HideCursor();
    int display_logged = 0;

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, AUDIO_RATE };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (stream) SDL_ResumeAudioStreamDevice(stream);
    /* A replay's clock must be known before creating either audio backend. */
    if (replay) {
        inputlog_read_header(replay, &ips, &time_us);
        if (!inputlog_read_cadence(replay, &sim_hz)) {
            fprintf(stderr, "%s has an invalid simulation cadence\n", replay);
            SDL_Quit(); return 2;
        }
        char ids[256];
        inputlog_read_fixes(replay, ids, sizeof ids);
        if (replay_fixes_cli) {
            fprintf(stderr, "replay fixes: the command line's set, not the log's (%s)\n", ids[0] ? ids : "none");
        } else {
            char before[256], after[256];
            fixes_enabled(before, sizeof before);
            if (!fixes_enable_list(ids)) {
                fprintf(stderr, "%s names an unknown fix: %s\n", replay, ids);
                SDL_Quit(); return 2;
            }
            fixes_enabled(after, sizeof after);
            /* a replay runs the fixes its log records so that it reproduces the session; --fix and --fixes only add to them */
            if (cli_fix_flags && strcmp(before, after))
                fprintf(stderr, "replay fixes: %s records [%s], which are on whatever --fix/--fixes say; "
                        "--replay-fixes cli runs only the command line's set\n", replay, ids);
        }
    }
    H.audio = audio_create_backend(ips, opl_backend);
    if (H.audio) audio_set_speaker_model(H.audio, speaker_model);
    if (g_adlib_effects < 0) g_adlib_effects = g_setup_sound == '3' && !replay;   /* the default: with Roland chosen, not in a replay */
    if (g_adlib_effects > 0) {                      /* the AdLib sidecar: ASOUND.117 from the install, a chip of its own */
        char path[1024];
        snprintf(path, sizeof path, "%s/ASOUND.117", data);
        FILE *df = fopen(path, "rb");
        uint8_t *dfile = NULL; long dlen = 0;
        if (df) { fseek(df, 0, SEEK_END); dlen = ftell(df); rewind(df); dfile = dlen > 0 ? malloc((size_t)dlen) : NULL;
                  if (dfile && fread(dfile, 1, (size_t)dlen, df) != (size_t)dlen) { free(dfile); dfile = NULL; } fclose(df); }
        g_side_audio = audio_create_backend(ips, opl_backend);
        if (dfile && g_side_audio) g_side = sidecar_new_adlib(dfile, (size_t)dlen, side_opl, NULL);
        free(dfile);
        if (!g_side) { fprintf(stderr, "--adlib-effects: could not start the AdLib driver from %s\n", path); audio_destroy(g_side_audio); g_side_audio = NULL; }
    }
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
    hooks.module_load = app_module_load;
    hooks.exec = on_exec_hook;
    hooks.file_data = fixes_file_data;
    if (g_replay) {
        drawfeed_init(&g_feed, H.mem);
        drawlive_init(&g_live);
        g_live.interp = g_replay == 2;
        g_live.extrapolate = g_age;
        if (g_scale) drawlive_set_scale(&g_live, g_scale);
        drawlive_set_line_width(&g_live, g_line_width);
        if (g_rerun) { drawfeed_set_rerun(&g_feed); drawlive_set_rerun(&g_live); }
        observe_set(&g_feed.obs);
    }
    /* A replay brings its own speed and boot time; a recording writes ours. */
    if (replay) g_setup_sound = 0;                  /* a replay brings its own inputs, SETUP's answers included */
    if (!time_us) time_us = machine_local_time_us();
    if (!replay) save_fixset_check(save, win);      /* a replay brings its recorded set */
    if (!machine_boot(&H.m, H.mem, data, save, "F117.COM", ips, time_us, &hooks)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", H.m.fault, win);
        return 1;
    }
    H.m.sim_hz = sim_hz;
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
        snprintf(g_shots_dir, sizeof g_shots_dir, "%s/../screenshots", save);
        { const char *b = strrchr(dir, '/'); snprintf(g_session_name, sizeof g_session_name, "%s", b ? b + 1 : dir); }
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
            inputlog_cadence(H.record, sim_hz);
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
    if (g_replay == 2) {
        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
        const double hz = mode && mode->refresh_rate >= 10 ? mode->refresh_rate : 60.0;
        g_frame_ips = (uint64_t)((double)H.m.ips / hz);
        if (!getenv("F117_PRESENT_THREADS_OFF")) {
            g_pw.go = SDL_CreateSemaphore(0);
            g_pw.done = SDL_CreateSemaphore(0);
            if (g_pw.go && g_pw.done) g_pw.thread = SDL_CreateThread(present_worker_main, "present-draw", &g_pw);
            for (int k = 0; k < PAL_HELPERS; k++) {
                g_pal[k].go = SDL_CreateSemaphore(0);
                g_pal[k].done = SDL_CreateSemaphore(0);
                if (g_pal[k].go && g_pal[k].done)
                    g_pal[k].thread = SDL_CreateThread(pal_helper_main, "present-palette", &g_pal[k]);
            }
            if (!getenv("F117_REMASTER_THREADS_OFF")) {                  /* the Remastered composite on four threads */
                int made = 0;
                for (int k = 0; k < PAR_HELPERS; k++) {
                    g_par[k].go = SDL_CreateSemaphore(0);
                    g_par[k].done = SDL_CreateSemaphore(0);
                    if (g_par[k].go && g_par[k].done) { g_par[k].thread = SDL_CreateThread(par_helper_main, "remaster-rows", &g_par[k]); made += g_par[k].thread != NULL; }
                }
                if (made == PAR_HELPERS) remaster_live_set_parallel(par_run, 1 + PAR_HELPERS);
            }
        }
    }
    const uint64_t start_ns = SDL_GetTicksNS();
    uint64_t paused_ns = 0, pause_began = 0;
    uint64_t discarded_clocks = 0;
    g_loop_timing = getenv("F117_LOOP_TIMING") != NULL;
    int replay_x4 = 4;                              /* a replay's speed in quarters: 4 is normal */
    int jump_typing = 0;                            /* a replay: G pressed, the second to jump to being typed */
    unsigned long jump_secs = 0;
    if (getenv("F117_REPLAY_START_X4")) replay_x4 = atoi(getenv("F117_REPLAY_START_X4"));   /* diagnostic: start a replay fast */
    uint64_t vt_ns = 0, vt_last = 0;                /* a replay's own clock, run at that speed */
    uint64_t seek_target = player && seek_secs > 0 ? (uint64_t)(seek_secs * (double)ips) : 0;   /* a replay runs flat out to here */
    int tabs_on = 0, esc_swallow = 0, switch_to_settings = 0;      /* the Esc menu: settings and keys as two tabs (a flight only: the original does nothing with Esc there) */
    int running = 1, paused = 0, menu_paused = 0, diag_frame = 0, ref_open = 0, ref_page = 0, ref_paused = 0, ref_frame = 0, rm_frame = 0, fx_frame = 0, wa_frame = 0, rp_frame = 0, sk_frame = 0;
    int mouse_buttons = 0;

    while (running) {
        /* F117_MENU_AUTOTEST=1 (diagnostic): a few frames in, press Ctrl+Alt+S, move to Fullscreen, change it,
         * move to Save and press Enter, as a player's keys would arrive; F117_MENU_SHOT=FILE.ppm saves the
         * menu's picture while it is open. Lets the real menu path run in a window-less check. */
        static int diag_next;
        int diag_rows = 0;
        settings_defs(&diag_rows);
        /* open; down to Fullscreen (row 3); change it; down to Save (row diag_rows); Enter */
        const int diag_steps = 1 + 3 + 1 + (diag_rows - 3) + 1;
        if (getenv("F117_MENU_AUTOTEST") && ++diag_frame >= 5 && diag_next < diag_steps) {
            const int k = diag_next++;
            SDL_Event e;
            SDL_zero(e);
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.scancode = k == 0 ? SDL_SCANCODE_S : k == 4 ? SDL_SCANCODE_RIGHT : k == diag_steps - 1 ? SDL_SCANCODE_RETURN : SDL_SCANCODE_DOWN;
            e.key.mod = k == 0 ? (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT) : 0;
            SDL_PushEvent(&e);
        }
        /* F117_ESC_AUTOTEST=1 (diagnostic, with F117_ESC_ANYWHERE=1): Esc, Tab, Tab, Esc one every 10 frames from frame 10, and a line of
         * state after each ([esc] settings open, keys open, tab strip, paused). */
        {
            static int esc_frame;
            if (getenv("F117_ESC_AUTOTEST")) {
                ++esc_frame;
                if (esc_frame % 10 == 0 && esc_frame >= 10 && esc_frame <= 50) {
                    static const SDL_Scancode seq[5] = { SDL_SCANCODE_GRAVE, SDL_SCANCODE_TAB, SDL_SCANCODE_TAB, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_ESCAPE };   /* the last Esc, with the menu closed, belongs to the game */
                    SDL_Event e; SDL_zero(e); e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = seq[esc_frame / 10 - 1];
                    SDL_PushEvent(&e);
                }
                if (esc_frame % 10 == 5 && esc_frame <= 55)
                    fprintf(stderr, "[esc] after key %d: settings %d, keys %d, strip %d, paused %d\n", esc_frame / 10, g_set.open, ref_open, tabs_on, paused);
            }
        }
        /* F117_REF_AUTOTEST=1 (diagnostic): press Ctrl+Alt+K, then Right twice, so the key reference's third page is
         * up; F117_MENU_SHOT=FILE.ppm saves its picture. */
        {
            static int ref_next;
            if (getenv("F117_REF_AUTOTEST") && ++ref_frame >= 5 && ref_next < 3) {
                const int k = ref_next++;
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.scancode = k == 0 ? SDL_SCANCODE_K : SDL_SCANCODE_RIGHT;
                e.key.mod = k == 0 ? (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT) : 0;
                SDL_PushEvent(&e);
            }
        }
        /* F117_REMAP_AUTOTEST=1 (diagnostic): open the menu, choose Remap a key, press Q then W, close it. */
        {
            static int rm_next;
            int rm_rows = 0;
            settings_defs(&rm_rows);
            if (getenv("F117_REMAP_AUTOTEST") && ++rm_frame >= 5 && rm_next < rm_rows + 7) {
                const int k = rm_next++;
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_EVENT_KEY_DOWN;
                if (k == 0) { e.key.scancode = SDL_SCANCODE_S; e.key.mod = (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT); }
                else if (k <= rm_rows + 2) e.key.scancode = SDL_SCANCODE_DOWN;       /* to the Remap a key row */
                else if (k == rm_rows + 3) e.key.scancode = SDL_SCANCODE_RETURN;
                else if (k == rm_rows + 4) e.key.scancode = SDL_SCANCODE_Q;
                else if (k == rm_rows + 5) e.key.scancode = SDL_SCANCODE_W;
                else e.key.scancode = SDL_SCANCODE_ESCAPE;
                SDL_PushEvent(&e);
            }
        }
        /* F117_FIXES_AUTOTEST=1 (diagnostic): open the menu, choose Choose fixes, switch the first fix, come back. */
        {
            static int fx_next;
            int fx_rows = 0;
            settings_defs(&fx_rows);
            if (getenv("F117_FIXES_AUTOTEST") && ++fx_frame >= 5 && fx_next < fx_rows + 8) {
                const int k = fx_next++;
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_EVENT_KEY_DOWN;
                if (k == 0) { e.key.scancode = SDL_SCANCODE_S; e.key.mod = (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT); }
                else if (k <= fx_rows + 4) e.key.scancode = SDL_SCANCODE_DOWN;       /* to the Choose fixes row */
                else if (k == fx_rows + 5) e.key.scancode = SDL_SCANCODE_RETURN;     /* open the picker */
                else if (k == fx_rows + 6) e.key.scancode = SDL_SCANCODE_RETURN;     /* switch the first fix */
                else e.key.scancode = SDL_SCANCODE_ESCAPE;                           /* back to the menu */
                SDL_PushEvent(&e);
            }
        }
        /* F117_WATCH_AUTOTEST=1 (diagnostic): open the menu, choose Watch a flight, take the second one in the list. */
        {
            static int wa_next;
            int wa_rows = 0;
            settings_defs(&wa_rows);
            if (getenv("F117_WATCH_AUTOTEST") && ++wa_frame >= 5 && wa_next < wa_rows + 9) {
                const int k = wa_next++;
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_EVENT_KEY_DOWN;
                if (k == 0) { e.key.scancode = SDL_SCANCODE_S; e.key.mod = (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT); }
                else if (k <= wa_rows + 5) e.key.scancode = SDL_SCANCODE_DOWN;       /* to the Watch a flight row */
                else if (k == wa_rows + 6) e.key.scancode = SDL_SCANCODE_RETURN;     /* open the list */
                else if (k == wa_rows + 7) e.key.scancode = SDL_SCANCODE_DOWN;       /* the second flight */
                else e.key.scancode = SDL_SCANCODE_RETURN;
                SDL_PushEvent(&e);
            }
        }
        /* F117_REPLAY_AUTOTEST=1 (diagnostic, with --watch): at frames 200, 400 and 600 press -, - and 0, so the clock
         * lines the speed keys print show the replay run at 1x, then half, a quarter, and normal again. */
        {
            if (getenv("F117_REPLAY_AUTOTEST")) {
                ++rp_frame;
                if (rp_frame == 200 || rp_frame == 400 || rp_frame == 600 || rp_frame == 620) {
                    SDL_Event e;
                    SDL_zero(e);
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.scancode = rp_frame == 620 ? SDL_SCANCODE_R : rp_frame == 600 ? SDL_SCANCODE_0 : SDL_SCANCODE_MINUS;
                    if (rp_frame == 620) e.key.mod = (SDL_Keymod)(SDL_KMOD_LCTRL | SDL_KMOD_LALT);   /* Ctrl+Alt+R: start again */
                    SDL_PushEvent(&e);
                }
            }
        }
        /* F117_SEEK_AUTOTEST=1 (diagnostic, with --watch): at frame 100 press ], at frame 400 press [ (use
         * F117_RESTART_DRYRUN=1 to see the restart it asks for). */
        {
            if (getenv("F117_JUMP_AUTOTEST")) {      /* diagnostic, with --watch: frames 100 to 103 type G 6 0 Enter (go to second 60) */
                ++sk_frame;
                static const SDL_Scancode typed[4] = { SDL_SCANCODE_G, SDL_SCANCODE_6, SDL_SCANCODE_0, SDL_SCANCODE_RETURN };
                if (sk_frame >= 100 && sk_frame < 104) {
                    SDL_Event e;
                    SDL_zero(e);
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.scancode = typed[sk_frame - 100];
                    SDL_PushEvent(&e);
                }
            }
            if (getenv("F117_SEEK_AUTOTEST")) {
                ++sk_frame;
                if (sk_frame == 100 || sk_frame == 400) {
                    SDL_Event e;
                    SDL_zero(e);
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.scancode = sk_frame == 100 ? SDL_SCANCODE_RIGHTBRACKET : SDL_SCANCODE_LEFTBRACKET;
                    SDL_PushEvent(&e);
                }
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                if (g_set.open && g_set.padkey_stage == 1) {        /* Controller button as key: this is the button */
                    g_set.padkey_button = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? ev.gbutton.button : ev.jbutton.button;
                    if (g_set.padkey_button >= 0 && g_set.padkey_button <= 31) {
                        g_set.padkey_stage = 2;
                        snprintf(g_set.note, sizeof g_set.note, "Button %d", g_set.padkey_button);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                display_logged = 0;
                break;
            case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
                const int down = ev.type == SDL_EVENT_KEY_DOWN;
                const SDL_Keymod mod = ev.key.mod;
                if (down && !live) {                        /* a replay: speed keys, and Ctrl+Alt+R to start it again */
                    const SDL_Scancode rs = ev.key.scancode;
                    int changed = 1;
                    if (jump_typing) {                                   /* G, then the second to go to, then Enter (Esc gives up) */
                        int digit = -1;
                        if (rs >= SDL_SCANCODE_1 && rs <= SDL_SCANCODE_9) digit = rs - SDL_SCANCODE_1 + 1;
                        else if (rs == SDL_SCANCODE_0 || rs == SDL_SCANCODE_KP_0) digit = 0;
                        else if (rs >= SDL_SCANCODE_KP_1 && rs <= SDL_SCANCODE_KP_9) digit = rs - SDL_SCANCODE_KP_1 + 1;
                        if (digit >= 0) { if (jump_secs < 100000) jump_secs = jump_secs * 10 + (unsigned)digit; }
                        else if (rs == SDL_SCANCODE_BACKSPACE) jump_secs /= 10;
                        else if (rs == SDL_SCANCODE_ESCAPE) jump_typing = 0;
                        else if (rs == SDL_SCANCODE_RETURN || rs == SDL_SCANCODE_KP_ENTER) {
                            jump_typing = 0;
                            const double now_s = (double)H.m.cpu.icount / (double)ips;
                            if ((double)jump_secs > now_s) {              /* on: flat out, muted, to there */
                                seek_target = (uint64_t)jump_secs * ips;
                                fprintf(stderr, "replay seek to second %lu (clock %llu)\n", jump_secs, (unsigned long long)seek_target);
                            } else {                                      /* back: start again, flat out to there */
                                char secs[32];
                                snprintf(secs, sizeof secs, "%lu", jump_secs);
                                restart_game(orig_argv, "--seek", secs);
                                running = 0;
                            }
                        }
                        if (jump_typing) {
                            char title[96];
                            snprintf(title, sizeof title, "F-117A - replay: go to second %lu_   (Enter goes, Esc cancels)", jump_secs);
                            SDL_SetWindowTitle(win, title);
                        } else SDL_SetWindowTitle(win, "F-117A - replay");
                        break;
                    }
                    if (rs == SDL_SCANCODE_G && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) { jump_typing = 1; jump_secs = 0; changed = 0; SDL_SetWindowTitle(win, "F-117A - replay: go to second _   (Enter goes, Esc cancels)"); }
                    else if (rs == SDL_SCANCODE_EQUALS || rs == SDL_SCANCODE_KP_PLUS) { if (replay_x4 < 64) replay_x4 *= 2; }
                    else if (rs == SDL_SCANCODE_MINUS || rs == SDL_SCANCODE_KP_MINUS) { if (replay_x4 > 1) replay_x4 /= 2; }
                    else if (rs == SDL_SCANCODE_0 || rs == SDL_SCANCODE_KP_0) replay_x4 = 4;
                    else if (rs == SDL_SCANCODE_RIGHTBRACKET) {          /* ] jumps 30 seconds on: flat out, muted */
                        seek_target = H.m.cpu.icount + 30 * ips;
                        fprintf(stderr, "replay seek to clock %llu, wall %llu ms\n", (unsigned long long)seek_target, (unsigned long long)SDL_GetTicks());
                        changed = 0;
                    } else if (rs == SDL_SCANCODE_LEFTBRACKET) {         /* [ jumps 30 seconds back: start again, flat out to there */
                        const double now_s = (double)H.m.cpu.icount / (double)ips;
                        char secs[32];
                        snprintf(secs, sizeof secs, "%.2f", now_s > 30 ? now_s - 30 : 0.0);
                        restart_game(orig_argv, "--seek", secs);
                        running = 0;
                        changed = 0;
                    }
                    else if (rs == SDL_SCANCODE_R && (mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_ALT)) {
                        restart_game(orig_argv, NULL, NULL);
                        running = 0;
                        changed = 0;
                    } else changed = 0;
                    if (changed) {
                        char title[96];
                        snprintf(title, sizeof title, "F-117A - replay at %gx   (+ faster, - slower, 0 normal, Ctrl+Alt+R again)", replay_x4 / 4.0);
                        SDL_SetWindowTitle(win, title);
                        fprintf(stderr, "replay speed %gx at clock %llu, wall %llu ms\n", replay_x4 / 4.0, (unsigned long long)H.m.cpu.icount, (unsigned long long)(SDL_GetTicks()));
                        break;
                    }
                }
                if (down && ev.key.scancode == SDL_SCANCODE_RETURN && (mod & SDL_KMOD_ALT) && !(mod & SDL_KMOD_CTRL)) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen);
                    break;
                }
                if (down && (ev.key.scancode == SDL_SCANCODE_ESCAPE || ev.key.scancode == SDL_SCANCODE_GRAVE) && (g_set.open || ref_open)) esc_swallow = 1;   /* an Esc the menu takes is not the game's */
                if (!down && (ev.key.scancode == SDL_SCANCODE_ESCAPE || ev.key.scancode == SDL_SCANCODE_GRAVE) && esc_swallow) { esc_swallow = 0; break; }   /* the key-up of the Esc that opened or closed the menu */
                if (down && ev.key.scancode == SDL_SCANCODE_GRAVE && tabs_on && (g_set.open || ref_open) && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) {
                    SDL_Event back; SDL_zero(back); back.type = SDL_EVENT_KEY_DOWN; back.key.scancode = SDL_SCANCODE_ESCAPE;   /* the ` key closes the menu as Esc does (one level back) */
                    SDL_PushEvent(&back);
                    break;
                }
                /* The ` (tilde) key opens the menu, in a mission and in the front-end screens alike. Esc is NOT taken while the menu is closed: the game uses it (it cancels the flight's
                 * Alt+Q quit dialog, tested with the quit route: Alt+Q, Esc, Y does not exit), PLAYER skips its title animation with it and
                 * START's text entry cancels on it. In the menu itself Esc closes or backs out, as before. */
                if (down && ev.key.scancode == SDL_SCANCODE_GRAVE && live && settings_path[0] && !g_set.open && !ref_open && tex_menu && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) {
                    tabs_on = 1; switch_to_settings = 1; esc_swallow = 1;                   /* ` in flight: the menu, on its Settings tab */
                }
                /* Tab with the menu closed: a screenshot (as Ctrl+Alt+C). The game never sees it; in flight it has no use for Tab (the
                 * key dispatcher's chain, VGAME CE65, goes from 0E08h Backspace to 1100h Alt+W, so 0F09h takes no arm). */
                if (ev.key.scancode == SDL_SCANCODE_TAB && live && !g_set.open && !ref_open && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) {
                    if (down && !ev.key.repeat) g_shot_now = 1;
                    break;
                }
                if (down && tabs_on && ev.key.scancode == SDL_SCANCODE_TAB) {              /* Tab: the other tab (not from inside a sub-screen) */
                    if (g_set.open && !g_set.picker && !g_set.flight_picker && !g_set.text_picker && !g_set.remap_stage && !g_set.padkey_stage) {
                        settings_key_event(&g_set, SKEY_ESC);                              /* close Settings without saving, keep the pause */
                        SDL_StopTextInput(win);
                        if (menu_paused) { paused_ns += SDL_GetTicksNS() - pause_began; paused = 0; menu_paused = 0; }
                        ref_open = 1; ref_page = 0;
                        if (!paused) { paused = 1; pause_began = SDL_GetTicksNS(); ref_paused = 1; }
                    } else if (ref_open) {
                        ref_open = 0;
                        if (ref_paused) { paused_ns += SDL_GetTicksNS() - pause_began; paused = 0; ref_paused = 0; }
                        switch_to_settings = 1;
                    }
                    if (!switch_to_settings) break;
                }
                if ((down && (mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_ALT) && ev.key.scancode == SDL_SCANCODE_S &&
                    live && settings_path[0] && !g_set.open && !ref_open && tex_menu) ||
                    (switch_to_settings && live && settings_path[0] && !g_set.open && !ref_open && tex_menu)) {
                    switch_to_settings = 0;
                    char *text = config_read_file(settings_path);          /* NULL if the file is not there yet */
                    settings_load(&g_set, text);
                    free(text);
                    settings_open(&g_set);
                    SDL_StartTextInput(win);                              /* for the Folders screen; the other screens ignore typed text */
                    settings_set_live(&g_set, "fullscreen", fullscreen ? 1 : 0);
                    {   /* the menu shows what is running: options given on the command line (a .bat) are not in the file, so mark them */
                        char b[40];
                        settings_mark_saved(&g_set, "presentation", g_remaster ? "remastered" : g_replay == 2 ? "enhanced" : "original");
                        if (g_replay == 2 || g_remaster) {
                            snprintf(b, sizeof b, "%d", g_scale);
                            settings_mark_saved(&g_set, "present-scale", g_scale > 0 ? b : "auto");
                        }
                        if (g_setup_sound) settings_mark_saved(&g_set, "sound", g_setup_sound == '1' ? "speaker" : g_setup_sound == '2' ? "adlib" : "roland");
                        settings_mark_saved(&g_set, "crt", g_crt ? "yes" : "no");
                        settings_mark_saved(&g_set, "crt-mask", g_crt_mask ? "yes" : "no");
                        settings_mark_saved(&g_set, "crt-glow", g_crt_glow ? "yes" : "no");
                        settings_mark_saved(&g_set, "subtitles", g_subtitles ? "yes" : "no");
                        settings_mark_saved(&g_set, "no-aspect", g_aspect_on ? "no" : "yes");
                        settings_mark_saved(&g_set, "adlib-effects", g_adlib_effects > 0 ? "yes" : "no");
                        snprintf(b, sizeof b, "%llu", (unsigned long long)ips);
                        settings_mark_saved(&g_set, "ips", b);
                        snprintf(b, sizeof b, "%d", g_pad.deadzone);
                        settings_mark_saved(&g_set, "pad-deadzone", b);
                        snprintf(b, sizeof b, "%d", g_pad.fire1);
                        settings_mark_saved(&g_set, "pad-fire1", b);
                        snprintf(b, sizeof b, "%d", g_pad.fire2);
                        settings_mark_saved(&g_set, "pad-fire2", b);
                        settings_mark_saved(&g_set, "pad-invert-x", g_pad.invert_x ? "yes" : "no");
                        settings_mark_saved(&g_set, "pad-invert-y", g_pad.invert_y ? "yes" : "no");
                        settings_mark_saved(&g_set, "engine", H.m.engine == ENGINE_INTERP ? "interp" : "recomp");
                    }
                    fill_fix_picker(&g_set);
                    fill_flight_list(&g_set, save);
                    if (!paused) { paused = 1; pause_began = SDL_GetTicksNS(); menu_paused = 1; }
                    break;
                }
                if (down && (mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_ALT) && ev.key.scancode == SDL_SCANCODE_K &&
                    live && !g_set.open && tex_menu) {              /* the on-screen key reference */
                    ref_open = !ref_open;
                    ref_page = 0;
                    if (ref_open && !paused) { paused = 1; pause_began = SDL_GetTicksNS(); ref_paused = 1; }
                    else if (!ref_open && ref_paused) { paused_ns += SDL_GetTicksNS() - pause_began; paused = 0; ref_paused = 0; }
                    break;
                }
                if (ref_open) {                                     /* the reference has the keyboard */
                    if (down) {
                        const SDL_Scancode sc = ev.key.scancode;
                        if (sc == SDL_SCANCODE_RIGHT && ref_page + 1 < KEYREF_PAGES) ref_page++;
                        else if (sc == SDL_SCANCODE_LEFT && ref_page > 0) ref_page--;
                        else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RETURN) {
                            ref_open = 0;
                            if (ref_paused) { paused_ns += SDL_GetTicksNS() - pause_began; paused = 0; ref_paused = 0; }
                        }
                    }
                    break;
                }
                if (g_set.open) {                           /* the menu has the keyboard */
                    if (down) {
                        if (g_set.padkey_stage) {           /* Controller button as key: stage 1 waits for a button, stage 2 for this key */
                            const SDL_Scancode psc = ev.key.scancode;
                            if (psc == SDL_SCANCODE_ESCAPE) g_set.padkey_stage = 0;
                            else if (g_set.padkey_stage == 2) {
                                padkeys_set(g_set.padkey_button, psc);
                                padkeys_save(settings_path);
                                snprintf(g_set.note, sizeof g_set.note, "Button %d now presses %s", g_set.padkey_button, SDL_GetScancodeName(psc));
                                g_set.padkey_stage = 0;
                            }
                            break;
                        }
                        if (g_set.remap_stage) {            /* Remap a key: this press is one of its two keys */
                            const SDL_Scancode rsc = ev.key.scancode;
                            if (rsc == SDL_SCANCODE_ESCAPE) g_set.remap_stage = 0;
                            else if (g_set.remap_stage == 1) { g_set.remap_from = (int)rsc; g_set.remap_stage = 2; }
                            else {
                                char pair[160], cur[400] = "";
                                snprintf(pair, sizeof pair, "%s:%s", SDL_GetScancodeName((SDL_Scancode)g_set.remap_from), SDL_GetScancodeName(rsc));
                                if (remap_add(pair) > 0) {
                                    char *text = config_read_file(settings_path);
                                    if (text) config_get(text, "remap", cur, sizeof cur);
                                    const size_t used = strlen(cur);
                                    snprintf(cur + used, sizeof cur - used, "%s%s", used ? ", " : "", pair);
                                    char *out = config_set(text ? text : "", "remap", cur);
                                    FILE *rf = out ? fopen(settings_path, "wb") : NULL;
                                    if (rf) { fputs(out, rf); fclose(rf); snprintf(g_set.note, sizeof g_set.note, "Added %s", pair); }
                                    else snprintf(g_set.note, sizeof g_set.note, "Could not write f117a.ini");
                                    free(text);
                                    free(out);
                                } else snprintf(g_set.note, sizeof g_set.note, "Those keys have no name the game can use");
                                g_set.remap_stage = 0;
                            }
                            break;
                        }
                        const SDL_Scancode sc = ev.key.scancode;
                        const int k = sc == SDL_SCANCODE_UP ? SKEY_UP : sc == SDL_SCANCODE_DOWN ? SKEY_DOWN :
                                      sc == SDL_SCANCODE_LEFT ? SKEY_LEFT : sc == SDL_SCANCODE_RIGHT ? SKEY_RIGHT :
                                      sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER ? SKEY_ENTER :
                                      sc == SDL_SCANCODE_ESCAPE ? SKEY_ESC : sc == SDL_SCANCODE_BACKSPACE ? SKEY_BACKSPACE : -1;
                        if (sc == SDL_SCANCODE_V && (mod & SDL_KMOD_CTRL) && g_set.text_picker && g_set.text_edit >= 0) {   /* a paste into the folder being typed */
                            char *clip = SDL_GetClipboardText();
                            if (clip) { settings_text_input(&g_set, clip); SDL_free(clip); }
                        } else if (k >= 0) settings_key_event(&g_set, (settings_key)k);
                        if (g_set.picks_changed && !g_set.picker) { g_set.picks_changed = 0; write_custom_fixes(&g_set, settings_path); }
                        if (g_set.clear_padkeys) {          /* Clear controller keys: forget them now and in the file */
                            g_set.clear_padkeys = 0;
                            g_npadkeys = 0;
                            padkeys_save(settings_path);
                            snprintf(g_set.note, sizeof g_set.note, "Controller keys cleared");
                        }
                        if (g_set.clear_remaps) {           /* Clear key remaps: forget them now and in the file */
                            g_set.clear_remaps = 0;
                            remap_clear();
                            char *text = config_read_file(settings_path);
                            char *out = text ? config_set(text, "remap", NULL) : NULL;
                            FILE *rf = out ? fopen(settings_path, "wb") : NULL;
                            if (rf) { fputs(out, rf); fclose(rf); }
                            snprintf(g_set.note, sizeof g_set.note, "Key remaps cleared");
                            free(text);
                            free(out);
                        }
                        if (g_set.changed_live) {
                            g_set.changed_live = 0;
                            int n; const setting_def *d = settings_defs(&n);
                            for (int i = 0; i < n; i++)
                                if (!strcmp(d[i].key, "fullscreen")) {
                                    fullscreen = !strcmp(settings_value(&g_set, i), "yes");
                                    SDL_SetWindowFullscreen(win, fullscreen);
                                }
                        }
                        if (!g_set.open) {
                            SDL_StopTextInput(win);
                            if (menu_paused) { paused_ns += SDL_GetTicksNS() - pause_began; paused = 0; menu_paused = 0; }
                            if (g_set.watch_now) { restart_game(orig_argv, "--watch", g_set.watch_name[0] ? g_set.watch_name : "latest"); running = 0; }
                            if (g_set.saved_now) {
                                const int restart = g_set.want_restart;
                                char *text = config_read_file(settings_path);
                                char *out = settings_apply(&g_set, text);
                                FILE *f = out ? fopen(settings_path, "wb") : NULL;
                                if (f) { fputs(out, f); fclose(f); }
                                else SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", "Could not write f117a.ini", win);
                                free(text);
                                free(out);
                                if (f && restart && restart_wanted(win)) { restart_game(orig_argv, NULL, NULL); running = 0; }
                            }
                        }
                    }
                    break;
                }
                if (down && (mod & SDL_KMOD_CTRL) && (mod & SDL_KMOD_ALT)) {
                    if (ev.key.scancode == SDL_SCANCODE_F12) { running = 0; break; }
                    if (ev.key.scancode == SDL_SCANCODE_C) { g_shot_now = 1; break; }
                    if (ev.key.scancode == SDL_SCANCODE_P) {
                        paused = !paused;
                        if (paused) pause_began = SDL_GetTicksNS();
                        else paused_ns += SDL_GetTicksNS() - pause_began;
                        break;
                    }
                }
                if (!paused && live) key_event(remap_apply(ev.key.scancode), down);   /* repeats re-send the make code */
                break;
            }
            case SDL_EVENT_TEXT_INPUT:                  /* typed text for the Folders screen */
                if (g_set.open && g_set.text_picker && g_set.text_edit >= 0) settings_text_input(&g_set, ev.text.text);
                break;
            case SDL_EVENT_MOUSE_MOTION: {
                if (!live) break;
                int gx, gy;
                window_to_guest(ren, aspect, ev.motion.x, ev.motion.y, &gx, &gy);
                machine_mouse(&H.m, gx, gy, mouse_buttons, (int)ev.motion.xrel, (int)ev.motion.yrel);
                g_mouse_dirty = 1; g_mouse_sent_x = gx * 2; g_mouse_sent_y = gy;
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (!live) break;
                int bit = ev.button.button == SDL_BUTTON_LEFT ? 1 : ev.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) mouse_buttons |= bit; else mouse_buttons &= ~bit;
                int gx, gy;
                window_to_guest(ren, aspect, ev.button.x, ev.button.y, &gx, &gy);
                machine_mouse(&H.m, gx, gy, mouse_buttons, 0, 0);
                g_mouse_dirty = 1; g_mouse_sent_x = gx * 2; g_mouse_sent_y = gy;
                break;
            }
            default: break;
            }
        }
        if (!running) break;

        /* The analog stick on the game port: axes 0..255, buttons (controls.h: dead zone, inversion, fire buttons). */
        if ((pad || joy) && live) {
            int rx, ry;
            unsigned held = 0;
            if (pad) {
                rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
                ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
                if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)g_pad.fire1)) held |= 1u << g_pad.fire1;
                if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)g_pad.fire2)) held |= 1u << g_pad.fire2;
            } else {
                rx = SDL_GetJoystickAxis(joy, 0);
                ry = SDL_GetJoystickAxis(joy, 1);
                if (SDL_GetJoystickButton(joy, g_pad.fire1)) held |= 1u << g_pad.fire1;
                if (SDL_GetJoystickButton(joy, g_pad.fire2)) held |= 1u << g_pad.fire2;
            }
            for (int j = 0; j < g_npadkeys; j++) {              /* buttons that act as keys: a press or release of the key */
                const int now = pad ? SDL_GetGamepadButton(pad, (SDL_GamepadButton)g_padkeys[j].button)
                                    : SDL_GetJoystickButton(joy, g_padkeys[j].button);
                if (now != g_padkeys[j].held) {
                    g_padkeys[j].held = now;
                    if (!paused) key_event(g_padkeys[j].key, now);
                }
            }
            const unsigned b = controls_buttons(&g_pad, held);
            unsigned axis[4] = { controls_axis(&g_pad, rx, g_pad.invert_x), controls_axis(&g_pad, ry, g_pad.invert_y), 0x100, 0x100 };
            if (axis[0] != last_axis[0] || axis[1] != last_axis[1] || b != last_buttons) {
                machine_joystick(&H.m, 1, axis, b);
                last_axis[0] = axis[0]; last_axis[1] = axis[1]; last_buttons = b;
            }
        }

        if (player) {                                       /* a replay's own clock: real time passed times the speed */
            const uint64_t real = SDL_GetTicksNS();
            if (vt_last && !paused) vt_ns += (real - vt_last) * (uint64_t)replay_x4 / 4;
            vt_last = real;
        }
        /* Keep the machine's clock level with the wall clock. */
        if (!paused) {
            uint64_t now_ns = SDL_GetTicksNS() - start_ns - paused_ns;
            if (player) now_ns = vt_ns;                      /* a replay runs on its own clock, at the chosen speed */
            /* Never more than 100 ms behind: after a stall, catch up gently
             * rather than racing. */
            uint64_t target = host_clock_target(now_ns, ips, H.m.cpu.icount, &discarded_clocks);
            if (exit_after && target > exit_after) target = exit_after;
            if (seek_target) {                                 /* a seek: as fast as the machine goes until it gets there */
                if (H.m.cpu.icount >= seek_target) {
                    seek_target = 0;
                    vt_ns = (uint64_t)((double)H.m.cpu.icount * 1e9 / (double)ips);   /* then pace from here */
                    discarded_clocks = 0;
                    fprintf(stderr, "replay seek done at clock %llu, wall %llu ms\n", (unsigned long long)H.m.cpu.icount, (unsigned long long)SDL_GetTicks());
                } else target = H.m.cpu.icount + ips / 5 < seek_target ? H.m.cpu.icount + ips / 5 : seek_target;
            }
            const uint64_t lt_run0 = SDL_GetTicksNS();
            while (H.m.cpu.icount < target) {
                uint64_t until = H.m.cpu.icount + ips / 1000;  /* 1 ms slices */
                if (until > target) until = target;
                inputlog_feed(player, &H.m, until + ips);
                const uint64_t sl0 = g_loop_timing ? SDL_GetPerformanceCounter() : 0;
                int rc = machine_run(&H.m, until);
                if (g_loop_timing && (SDL_GetPerformanceCounter() - sl0) * 1250 > SDL_GetPerformanceFrequency()) {   /* a 1 ms slice that took over 0.8 ms (normally about 0.3) */
                    const uint32_t lin = ((uint32_t)H.m.cpu.seg[S_CS] << 4) + H.m.cpu.ip;
                    int k = 0;
                    while (k < g_lt_nslow && g_lt_slow[k].lin != lin) k++;
                    if (k == g_lt_nslow && k < 64) { g_lt_slow[k].lin = lin; g_lt_slow[k].cs = H.m.cpu.seg[S_CS]; g_lt_slow[k].ip = H.m.cpu.ip; g_lt_slow[k].n = 0; g_lt_nslow++; }
                    if (k < g_lt_nslow) g_lt_slow[k].n++;
                }
                if (g_replay == 2) {
                    if (g_pw.thread) drawlive_collect(&g_live, &g_feed);
                    else drawlive_update(&g_live, &g_feed);
                }
                if (rc == RUN_EXITED) { running = 0; break; }
                if (rc == RUN_FAULT) {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A: the machine stopped", H.m.fault, win);
                    running = 0;
                    break;
                }
            }
            g_lt_run += SDL_GetTicksNS() - lt_run0;
            mouse_follow_game(win, ren, aspect, replay != NULL || g_set.open);
            if (H.audio) {
                audio_advance(H.audio, H.m.cpu.icount);
                const int queued_before = stream ? SDL_GetAudioStreamQueued(stream) : 0;
                size_t produced_frames = 0;
                size_t n;
                if (g_side) { side_run(&H.m, ips); audio_advance(g_side_audio, H.m.cpu.icount); }
                while ((n = audio_take(H.audio, abuf, AUDIO_RATE)) > 0) {
                    if (g_side) {                                     /* the sidecar's AdLib effects over Roland's music */
                        static int16_t sbuf[AUDIO_RATE * 2];
                        const size_t got = audio_take(g_side_audio, sbuf, n);
                        for (size_t k = 0; k < got * 2; k++) {
                            const int v = abuf[k] + sbuf[k];
                            abuf[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                        }
                    }
                    macsound_mix(g_mac, abuf, n);                     /* the Mac recordings over the driver's output */
                    if (audio_dump && fwrite(abuf, 4, n, audio_dump) != n)
                        H.audio_failed = 1;
                    if (stream && replay_x4 == 4 && !seek_target) {   /* a replay off normal speed, or seeking, is silent */
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
            {   /* F117_POKE=CLOCK:PHYSADDR:HEXBYTES[,...] (diagnostic): write bytes into the machine's memory once, when the clock reaches CLOCK;
                 * for testing a guess about the game's state (the machine is not otherwise touched) */
                static const char *pk; static int pinit; static uint64_t pat;
                if (!pinit) { pinit = 1; pk = getenv("F117_POKE"); if (pk) pat = strtoull(pk, NULL, 0); }
                if (pk && pat && H.m.cpu.icount >= pat) {
                    pat = 0;
                    const char *q = strchr(pk, ':');
                    while (q) {
                        const unsigned long long addr = strtoull(q + 1, NULL, 16);
                        const char *hex = strchr(q + 1, ':');
                        if (!hex) break;
                        unsigned long long k = 0;
                        for (hex++; hex[0] && hex[1] && hex[0] != ',' ; hex += 2, k++) {
                            char b2[3] = { hex[0], hex[1], 0 };
                            H.m.mem[(addr + k) & 0xFFFFF] = (uint8_t)strtoul(b2, NULL, 16);
                        }
                        fprintf(stderr, "poked %llu bytes at %05llX, clock %llu\n", k, addr, (unsigned long long)H.m.cpu.icount);
                        q = strchr(hex, ',');
                    }
                }
            }
            {   /* F117_DUMP_RAM=CLOCK:FILE (diagnostic): the machine's 1 MB of memory once, when the clock reaches CLOCK */
                static const char *spec; static int init; static uint64_t at;
                if (!init) { init = 1; spec = getenv("F117_DUMP_RAM"); if (spec) at = strtoull(spec, NULL, 0); }
                if (spec && at && H.m.cpu.icount >= at) {
                    at = 0;
                    const char *colon = strchr(spec, ':');
                    FILE *df = colon ? fopen(colon + 1, "wb") : NULL;
                    if (df) { fwrite(H.m.mem, 1, 0x100000, df); fclose(df); fprintf(stderr, "memory dumped at clock %llu\n", (unsigned long long)H.m.cpu.icount); }
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
        int fine_ready = 0;
        if (g_replay == 2) {
            present_capture(&H.m, &H.frame);
            if (g_pw.thread) {
                /* The machine as it stands now: the next plan and its palette come from this, not from the
                 * frame shown below, which carries the previous plan's picture and palette (handing that on
                 * froze the palette: the driver's colour cycling, e.g. the gun rounds, and every fade). */
                static present_frame fresh;
                fresh = H.frame;
                drawlive_collect(&g_live, &g_feed);
                /* the picture planned a frame ago, drawn meanwhile by the worker */
                g_fine = NULL;
                if (g_pw.busy) {
                    SDL_WaitSemaphore(g_pw.done);
                    g_pw.busy = 0;
                    older = g_pw.plan.older; newer = g_pw.plan.newer; t_shown = g_pw.plan.t_shown;
                    got = g_pw.got;
                    if (got) {
                        memcpy(H.frame.vram, g_pw.frame.vram, sizeof H.frame.vram);
                        memcpy(H.frame.dac, g_pw.frame.dac, sizeof H.frame.dac);
                        H.frame.text = g_pw.frame.text;
                        g_fine = remaster_apply(g_live.shown_hi);
                        if (g_fine && tex_fine && !H.frame.text) {
                            convert_fine(H.frame.dac, g_fine, fine_pixels, (size_t)320 * g_scale * 200 * g_scale);
                            remaster_true_colour(fine_pixels, (size_t)320 * g_scale * 200 * g_scale);
                            fine_ready = 1;
                        }
                    }
                }
                drawlive_publish(&g_live);          /* the worker is idle: nothing renders from the old steps */
                drawlive_plan plan;
                /* Interpolation shows the clock as it stands (a frame later than the drawing, which
                 * leads the display by one); extrapolation is a prediction and looks a frame ahead. */
                if (drawlive_plan_interp(&g_live, &H.m, &fresh, H.m.cpu.icount + (g_age ? g_frame_ips : 0), &plan)) {
                    drawlive_prepare_rerun(&g_live, &plan);   /* the copy runs here, on the machine's thread */
                    g_pw.plan = plan;
                    g_pw.frame = fresh;
                    g_pw.busy = 1;
                    SDL_SignalSemaphore(g_pw.go);
                }
            } else {
                drawlive_update(&g_live, &g_feed);
                got = drawlive_present_interp(&g_live, &H.m, &H.frame);
                g_fine = remaster_apply(g_live.shown_hi);
                const drawlive_step *a, *b;
                drawlive_steps(&g_live, &a, &b);
                if (a && b) {
                    const double span = b->end > a->end ? (double)(b->end - a->end) : 0;
                    older = a->seq; newer = b->seq;
                    t_shown = span > 0 ? ((double)H.m.cpu.icount - (double)b->end) / span : 0.0;
                }
            }
            g_presented += (uint64_t)got;
        }
        int w, h, wide = 0;
        SDL_Texture *show = tex;
        if (g_replay && !H.frame.text) {
            pal_note(&g_pal_shown, H.frame.dac);
            if (H.m.scan_valid) pal_note(&g_pal_scanned, H.m.scan_dac);
        }
        if (g_fine && tex_fine && !H.frame.text) {           /* the finer picture, with the frame's palette */
            w = 320 * g_scale; h = 200 * g_scale;
            if (!fine_ready) {
                convert_fine(H.frame.dac, g_fine, fine_pixels, (size_t)w * h);
                remaster_true_colour(fine_pixels, (size_t)w * h);
            }
            SDL_Rect src = { 0, 0, w, h };
            SDL_UpdateTexture(tex_fine, &src, crt_view(fine_pixels, w, h), w * 4);
            show = tex_fine;
            if (wide_make(ren, aspect)) { show = g_wtex; w = g_ww; h = g_wh; wide = 1; }   /* a window wider than 4:3 */
        } else {
            present_render(&H.frame, pixels, &w, &h, (int)((SDL_GetTicksNS() / 266666666ull) & 1));
            SDL_Rect src = { 0, 0, w, h };
            SDL_UpdateTexture(tex, &src, crt_view(pixels, w, h), w * 4);
        }
        /* F117_PRESENT_SHOTS=PREFIX (diagnostic): the picture just made ready for the display, from machine clock
         * F117_PRESENT_SHOTS_FROM, F117_PRESENT_SHOTS_COUNT of them, as PREFIX<icount>.ppm */
        {
            static const char *shots; static uint64_t from, left, step = 1, seen; static int shots_init;
            if (!shots_init) {
                shots_init = 1; shots = getenv("F117_PRESENT_SHOTS");
                const char *f = getenv("F117_PRESENT_SHOTS_FROM"), *c = getenv("F117_PRESENT_SHOTS_COUNT");
                from = f ? strtoull(f, NULL, 0) : 0; left = c ? strtoull(c, NULL, 0) : 60;
                { const char *st = getenv("F117_PRESENT_SHOTS_STEP"); if (st && atoi(st) > 1) step = (uint64_t)atoi(st); }   /* every Nth picture */
            }
            if (shots && left && H.m.cpu.icount >= from && (++seen % step) == 0) {
                char path[900]; snprintf(path, sizeof path, "%s%llu.ppm", shots, (unsigned long long)H.m.cpu.icount);
                FILE *sf = fopen(path, "wb");
                if (sf) {
                    const uint32_t *px = crt_view(show == g_wtex ? g_wpix : show == tex_fine ? fine_pixels : pixels, w, h);   /* as the window shows it: filters included */
                    fprintf(sf, "P6\n%d %d\n255\n", w, h);
                    for (int k = 0; k < w * h; k++) { const uint8_t rgb[3] = { (uint8_t)(px[k] >> 16), (uint8_t)(px[k] >> 8), (uint8_t)px[k] }; fwrite(rgb, 1, 3, sf); }
                    fclose(sf); left--;
                    if (getenv("F117_SHOT_TAGS") && g_live.shown_tag && g_scale > 0) {     /* diagnostic: the origin tags of the map's pixels in this picture */
                        unsigned long cnt[64] = { 0 };
                        const int sc = g_scale, W = 320 * sc;
                        for (int y = 111 * sc; y < 169 * sc; y++) for (int x = 76 * sc; x < 149 * sc; x++) cnt[g_live.shown_tag[(size_t)y * W + x] & 63]++;
                        fprintf(stderr, "[tags] clock %llu map pixels by origin tag:", (unsigned long long)H.m.cpu.icount);
                        for (int k = 0; k < 64; k++) if (cnt[k]) fprintf(stderr, " %d:%lu", k, cnt[k]);
                        fprintf(stderr, " (kind %d)\n", g_live.diag_kind);
                    }
                }
            }
        }
        if (g_shot_now) {
            g_shot_now = 0;
            char path[1000];
            if (g_shots_dir[0]) SDL_CreateDirectory(g_shots_dir);
            snprintf(path, sizeof path, "%s/%s-clock%llu.bmp", g_shots_dir[0] ? g_shots_dir : ".", g_session_name[0] ? g_session_name : "replay", (unsigned long long)H.m.cpu.icount);
            const uint32_t *px = crt_view(show == g_wtex ? g_wpix : show == tex_fine ? fine_pixels : pixels, w, h);
            SDL_Surface *sf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, (void *)px, w * 4);
            const int ok = sf && SDL_SaveBMP(sf, path);
            if (sf) SDL_DestroySurface(sf);
            fprintf(stderr, ok ? "[shot] %s\n" : "[shot] could not write %s\n", path);
        }
        SDL_FRect srcf = { 0, 0, (float)w, (float)h };
        g_wide_shown = wide;
        SDL_FRect dst = output_rect(ren, aspect);
        SDL_FRect dstv = dst;                                   /* the picture; dst stays the 4:3 shape the overlays use */
        if (wide) { int ow = 0, oh = 0; SDL_GetRenderOutputSize(ren, &ow, &oh); dstv.x = 0; dstv.y = 0; dstv.w = (float)ow; dstv.h = (float)oh; }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, show, &srcf, &dstv);
        if (!g_set.open && !ref_open) tabs_on = 0;
        if (g_set.open && tex_menu) {
            memset(menu_pixels, 0, sizeof menu_pixels);
            settings_render(&g_set, menu_pixels, 640, 400);
            if (tabs_on) keyref_tabs(0, menu_pixels, 640, 400);
            const char *shot = getenv("F117_MENU_SHOT");
            if (shot) {
                FILE *sf = fopen(shot, "wb");
                if (sf) {
                    fprintf(sf, "P6\n640 400\n255\n");
                    for (int k = 0; k < 640 * 400; k++) {
                        const uint8_t rgb[3] = { (uint8_t)(menu_pixels[k] >> 16), (uint8_t)(menu_pixels[k] >> 8), (uint8_t)menu_pixels[k] };
                        fwrite(rgb, 1, 3, sf);
                    }
                    fclose(sf);
                }
            }
            SDL_UpdateTexture(tex_menu, NULL, menu_pixels, 640 * 4);
            SDL_RenderTexture(ren, tex_menu, NULL, &dst);
        }
        if ((g_sub.ready || g_subtitles) && !g_set.open && !ref_open && tex_menu) {      /* the radio call as text */
            const char *line = sub_text(&g_sub, (unsigned long)SDL_GetTicks());
            if (line && getenv("F117_SUB_LOG")) {                   /* diagnostic: say each new line, with the clock */
                static char shown[96];
                if (strcmp(shown, line)) { snprintf(shown, sizeof shown, "%s", line); fprintf(stderr, "subtitle: %s at clock %llu\n", line, (unsigned long long)H.m.cpu.icount); }
            }
            if (line) {
                memset(menu_pixels, 0, sizeof menu_pixels);
                draw_subtitle(menu_pixels, line);
                static int sub_shot_done;
                if (getenv("F117_SUB_SHOT") && !sub_shot_done && strchr(line, ' ')) {     /* diagnostic: save the overlay once */
                    FILE *sf2 = fopen(getenv("F117_SUB_SHOT"), "wb");
                    if (sf2) {
                        fprintf(sf2, "P6\n640 400\n255\n");
                        for (int k = 0; k < 640 * 400; k++) {
                            const uint32_t v = menu_pixels[k];
                            const unsigned a = v >> 24;
                            const uint8_t rgb[3] = { (uint8_t)((((v >> 16) & 255) * a) / 255), (uint8_t)((((v >> 8) & 255) * a) / 255), (uint8_t)(((v & 255) * a) / 255) };
                            fwrite(rgb, 1, 3, sf2);
                        }
                        fclose(sf2);
                        sub_shot_done = 1;
                    }
                }
                SDL_UpdateTexture(tex_menu, NULL, menu_pixels, 640 * 4);
                SDL_RenderTexture(ren, tex_menu, NULL, &dst);
            }
        }
        if (ref_open && tex_menu) {
            memset(menu_pixels, 0, sizeof menu_pixels);
            keyref_render(ref_page, menu_pixels, 640, 400);
            if (tabs_on) keyref_tabs(1, menu_pixels, 640, 400);
            const char *rshot = getenv("F117_MENU_SHOT");
            if (rshot && getenv("F117_REF_AUTOTEST")) {
                FILE *sf = fopen(rshot, "wb");
                if (sf) {
                    fprintf(sf, "P6\n640 400\n255\n");
                    for (int k = 0; k < 640 * 400; k++) {
                        const uint8_t rgb[3] = { (uint8_t)(menu_pixels[k] >> 16), (uint8_t)(menu_pixels[k] >> 8), (uint8_t)menu_pixels[k] };
                        fwrite(rgb, 1, 3, sf);
                    }
                    fclose(sf);
                }
            }
            SDL_UpdateTexture(tex_menu, NULL, menu_pixels, 640 * 4);
            SDL_RenderTexture(ren, tex_menu, NULL, &dst);
        }
        const uint64_t lt_pr0 = SDL_GetTicksNS();
        SDL_RenderPresent(ren);
        g_lt_present += SDL_GetTicksNS() - lt_pr0;
        if (g_loop_timing) {                                  /* F117_LOOP_TIMING: where the loop's wall time goes, every 2 s */
            const uint64_t now = SDL_GetTicksNS();
            if (!g_lt_last) { g_lt_last = now; g_lt_icount = H.m.cpu.icount; g_lt_disc = discarded_clocks; }
            if (now - g_lt_last >= 2000000000ull) {
                fprintf(stderr, "[loop] wall %.1f s: icount +%.0fM (discarded +%.0fM); run %.0f ms, present %.0f ms, other %.0f ms, %u iterations; hooks: opl %.0f ms (%llu), speaker %.0f ms (%llu), vsync %.0f ms (%llu)\n",
                        (double)(now - start_ns) / 1e9, (double)(H.m.cpu.icount - g_lt_icount) / 1e6, (double)(discarded_clocks - g_lt_disc) / 1e6,
                        (double)g_lt_run / 1e6, (double)g_lt_present / 1e6, (double)(now - g_lt_last - g_lt_run - g_lt_present) / 1e6, g_lt_iter,
                        1e3 * (double)g_lt_hook[0] / (double)SDL_GetPerformanceFrequency(), (unsigned long long)g_lt_calls[0],
                        1e3 * (double)g_lt_hook[1] / (double)SDL_GetPerformanceFrequency(), (unsigned long long)g_lt_calls[1],
                        1e3 * (double)g_lt_hook[2] / (double)SDL_GetPerformanceFrequency(), (unsigned long long)g_lt_calls[2]);
                memset(g_lt_hook, 0, sizeof g_lt_hook); memset(g_lt_calls, 0, sizeof g_lt_calls);
                for (int pass = 0; pass < 4 && g_lt_nslow; pass++) {          /* the places slow slices ended, most first */
                    int best = 0;
                    for (int k = 1; k < g_lt_nslow; k++) if (g_lt_slow[k].n > g_lt_slow[best].n) best = k;
                    if (!g_lt_slow[best].n) break;
                    fprintf(stderr, "[loop]   slow slices ended at %04X:%04X (%05X) x%u\n", g_lt_slow[best].cs, g_lt_slow[best].ip, g_lt_slow[best].lin, g_lt_slow[best].n);
                    g_lt_slow[best].n = 0;
                }
                g_lt_nslow = 0;
                g_lt_last = now; g_lt_icount = H.m.cpu.icount; g_lt_disc = discarded_clocks; g_lt_run = g_lt_present = 0; g_lt_iter = 0;
            }
            g_lt_iter++;
        }
        if (g_present_log && !display_logged) {
            int width = 0, height = 0, vsync = 0;
            const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
            SDL_GetCurrentRenderOutputSize(ren, &width, &height);
            SDL_GetRenderVSync(ren, &vsync);
            fprintf(g_present_log, "# display renderer=%s output=%dx%d refresh=%.6f vsync=%d scale=%d\n",
                    SDL_GetRendererName(ren), width, height, mode ? mode->refresh_rate : 0.0, vsync, g_scale);
            if (mode && mode->refresh_rate >= 10) g_frame_ips = (uint64_t)((double)H.m.ips / mode->refresh_rate);
            display_logged = 1;
        }
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

    if (g_pw.thread) {
        if (g_pw.busy) SDL_WaitSemaphore(g_pw.done);
        g_pw.quit = 1;
        SDL_SignalSemaphore(g_pw.go);
        SDL_WaitThread(g_pw.thread, NULL);
    }
    for (int k = 0; k < PAL_HELPERS; k++)
        if (g_pal[k].thread) {
            g_pal[k].quit = 1;
            SDL_SignalSemaphore(g_pal[k].go);
            SDL_WaitThread(g_pal[k].thread, NULL);
        }
    for (int k = 0; k < PAR_HELPERS; k++)
        if (g_par[k].thread) {
            g_par[k].quit = 1;
            SDL_SignalSemaphore(g_par[k].go);
            SDL_WaitThread(g_par[k].thread, NULL);
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
        if (g_replay)
            fprintf(H.m.log, "[present] palettes: %d distinct presented, %d distinct scanned out (counted up to %d)\n",
                    g_pal_shown.n, g_pal_scanned.n, PAL_SET_MAX);
        if (g_rerun)
            fprintf(H.m.log, "[present] rerun: %llu in-between pictures drawn by the original on a copy, %llu fell back to "
                    "interpolation, %.0f instructions a copy\n", (unsigned long long)g_live.rerun_drawn,
                    (unsigned long long)g_live.rerun_fell_back,
                    g_live.rerun_drawn ? (double)g_live.rerun_insn / (double)g_live.rerun_drawn : 0.0);
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
    if (g_side) { if (getenv("F117_SOUND_LOG")) fprintf(stderr, "[sidecar] %llu requests (%llu failed), %llu ticks, %llu OPL writes\n", (unsigned long long)g_side_requests, (unsigned long long)g_side_failed, (unsigned long long)g_side_ticks, (unsigned long long)g_side_writes); sidecar_free(g_side); audio_destroy(g_side_audio); }
    SDL_Quit();
    return H.audio_failed ? 1 : 0;
}
