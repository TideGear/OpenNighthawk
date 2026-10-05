/* main.c - the game in a window: SDL3 video, input, audio and MIDI around
 * the emulated PC.
 *
 *   f117a [--data DIR] [--save DIR] [--engine recomp|interp] [--ips N]
 *         [--scale N] [--fullscreen] [--no-aspect] [--midi N] [--log FILE]
 *
 * The machine runs on its own clock (instructions); this loop keeps that
 * clock level with the wall clock, hands it the keyboard, mouse and stick
 * between slices, plays the audio it produced and shows the last frame its
 * VGA scanned out. None of that changes what the program computes: the
 * same inputs at the same instruction counts give the same run.
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
#include "inputlog.h"
#include "host_clock.h"

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
#ifdef _WIN32
    HMIDIOUT        midi_out;
#endif
} host_t;

static host_t H;

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

int main(int argc, char **argv)
{
    const char *data = NULL, *save = NULL, *log_path = NULL;
    uint64_t ips = MACHINE_DEFAULT_IPS;
    int scale = 3, fullscreen = 0, aspect = 1, midi_dev = -2;
    int engine = ENGINE_RECOMP;
    const char *coverage = NULL, *record = NULL, *replay = NULL;
    uint64_t time_us = 0, exit_after = 0;
    audio_opl_backend opl_backend = AUDIO_OPL_DBOPL;
    int no_record = 0;
    const char *mt32_control = NULL, *mt32_pcm = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
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
        else if (!strcmp(a, "--save") && v) { save = v; i++; }
        else if (!strcmp(a, "--log") && v) { log_path = v; i++; }
        else if (!strcmp(a, "--ips") && v) { ips = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--midi") && v) { midi_dev = atoi(v); i++; }
        else if (!strcmp(a, "--mt32-control") && v) { mt32_control = v; i++; }
        else if (!strcmp(a, "--mt32-pcm") && v) { mt32_pcm = v; i++; }
        else if (!strcmp(a, "--engine") && v) { engine = !strcmp(v, "interp") ? ENGINE_INTERP : ENGINE_RECOMP; i++; }
        else if (!strcmp(a, "--coverage") && v) { coverage = v; i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
        else if (!strcmp(a, "--no-aspect")) aspect = 0;
        else {
            fprintf(stderr,
                "usage: f117a [--data DIR] [--save DIR] [--engine recomp|interp] [--ips N]\n"
                "             [--scale N] [--fullscreen] [--no-aspect] [--midi N] [--log FILE]\n"
                "             [--record FILE | --no-record] [--replay FILE] [--time-us N]\n"
                "             [--exit-after CLOCKS] [--opl dbopl|nuked]\n"
                "             [--mt32-control FILE --mt32-pcm FILE]\n");
            return 2;
        }
    }
    if ((mt32_control != NULL) != (mt32_pcm != NULL) || (mt32_control && midi_dev != -2)) {
        fprintf(stderr, "Supply both --mt32-control and --mt32-pcm; choose one Roland output\n");
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
    SDL_HideCursor();

    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, AUDIO_RATE };
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (stream) SDL_ResumeAudioStreamDevice(stream);
    H.audio = audio_create_backend(ips, opl_backend);
    if (mt32_control) {
        char error[256];
        if (!audio_enable_mt32(H.audio, mt32_control, mt32_pcm, error, sizeof error)) {
            fprintf(stderr, "%s\n", error);
            audio_destroy(H.audio); SDL_Quit(); return 1;
        }
    }

#ifdef _WIN32
    if (midi_dev != -2) {
        UINT id = midi_dev < 0 ? MIDI_MAPPER : (UINT)midi_dev;
        if (midiOutOpen(&H.midi_out, id, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) H.midi_out = NULL;
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
    /* A replay brings its own speed and boot time; a recording writes ours. */
    if (replay) inputlog_read_header(replay, &ips, &time_us);
    if (!time_us) time_us = machine_local_time_us();
    if (H.audio) { audio_destroy(H.audio); H.audio = audio_create_backend(ips, opl_backend); }
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
                size_t n;
                while ((n = audio_take(H.audio, abuf, AUDIO_RATE)) > 0)
                    if (stream) SDL_PutAudioStreamData(stream, abuf, (int)(n * 4));
                /* The machine runs on the wall clock and the device on its
                 * own crystal; if the two drift apart by more than a
                 * quarter second, drop the backlog rather than lag. */
                if (stream && SDL_GetAudioStreamQueued(stream) > AUDIO_RATE * 4 / 4)
                    SDL_ClearAudioStream(stream);
            }
            if (H.audio_failed || (exit_after && H.m.cpu.icount >= exit_after)) running = 0;
        }

        /* Show the last frame the VGA scanned out. */
        if (!H.have_frame) present_capture(&H.m, &H.frame);
        int w, h;
        present_render(&H.frame, pixels, &w, &h, (int)((SDL_GetTicksNS() / 266666666ull) & 1));
        SDL_Rect src = { 0, 0, w, h };
        SDL_UpdateTexture(tex, &src, pixels, w * 4);
        SDL_FRect srcf = { 0, 0, (float)w, (float)h };
        SDL_FRect dst = output_rect(ren, aspect);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, &srcf, &dst);
        SDL_RenderPresent(ren);
    }

    if (H.record) fclose(H.record);
    inputlog_close(player);
    if (H.m.log) {
        /* The same summary line f117run prints, so a session can be
         * checked against a headless replay of its log. */
        const uint64_t hsh = machine_state_hash(&H.m);
        fprintf(H.m.log, "stopped at icount %llu; program %s; final hash %016llx\n",
                (unsigned long long)H.m.cpu.icount, dos_current_program(&H.m), (unsigned long long)hsh);
        recomp_report(&H.m, H.m.log);
    }
    recomp_shutdown(&H.m);
    machine_shutdown(&H.m);
#ifdef _WIN32
    if (H.midi_out) { midiOutReset(H.midi_out); midiOutClose(H.midi_out); }
#endif
    if (H.m.log) fclose(H.m.log);
    audio_destroy(H.audio);
    SDL_Quit();
    return H.audio_failed ? 1 : 0;
}
