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
    midistream      midi;
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
    (void)u; (void)icount;
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
static unsigned pc_scancode(SDL_Scancode s)
{
    static const uint8_t letters[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25,
        0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F,
        0x11, 0x2D, 0x15, 0x2C };
    static const uint8_t keypad[10] = {
        0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52 };
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 0x3B + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_0) return keypad[s - SDL_SCANCODE_KP_1];
    switch (s) {
    case SDL_SCANCODE_RETURN: return 0x1C;       case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_SPACE: return 0x39;        case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;       case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_SEMICOLON: return 0x27;    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;        case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;       case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;     case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;          case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45; case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;     case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;    case SDL_SCANCODE_LCTRL: return 0x1D;
    case SDL_SCANCODE_LSHIFT: return 0x2A;       case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_KP_DIVIDE: return 0x135;   case SDL_SCANCODE_KP_ENTER: return 0x11C;
    case SDL_SCANCODE_RCTRL: return 0x11D;       case SDL_SCANCODE_RALT: return 0x138;
    case SDL_SCANCODE_INSERT: return 0x152;      case SDL_SCANCODE_DELETE: return 0x153;
    case SDL_SCANCODE_HOME: return 0x147;        case SDL_SCANCODE_END: return 0x14F;
    case SDL_SCANCODE_PAGEUP: return 0x149;      case SDL_SCANCODE_PAGEDOWN: return 0x151;
    case SDL_SCANCODE_UP: return 0x148;          case SDL_SCANCODE_DOWN: return 0x150;
    case SDL_SCANCODE_LEFT: return 0x14B;        case SDL_SCANCODE_RIGHT: return 0x14D;
    case SDL_SCANCODE_PRINTSCREEN: return 0x137;
    default: return 0;
    }
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
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\F-117A Nighthawk Stealth Fighter 2.0",
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
    const char *coverage = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--data") && v) { data = v; i++; }
        else if (!strcmp(a, "--save") && v) { save = v; i++; }
        else if (!strcmp(a, "--log") && v) { log_path = v; i++; }
        else if (!strcmp(a, "--ips") && v) { ips = strtoull(v, NULL, 0); i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--midi") && v) { midi_dev = atoi(v); i++; }
        else if (!strcmp(a, "--engine") && v) { engine = !strcmp(v, "interp") ? ENGINE_INTERP : ENGINE_RECOMP; i++; }
        else if (!strcmp(a, "--coverage") && v) { coverage = v; i++; }
        else if (!strcmp(a, "--fullscreen")) fullscreen = 1;
        else if (!strcmp(a, "--no-aspect")) aspect = 0;
        else {
            fprintf(stderr,
                "usage: f117a [--data DIR] [--save DIR] [--engine recomp|interp] [--ips N]\n"
                "             [--scale N] [--fullscreen] [--no-aspect] [--midi N] [--log FILE]\n");
            return 2;
        }
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
    H.audio = audio_create(ips);

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
    if (!machine_boot(&H.m, H.mem, data, save, "F117.COM", ips,
                      (uint64_t)time(NULL) * 1000000ull, &hooks)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "F-117A", H.m.fault, win);
        return 1;
    }

    static uint32_t pixels[640 * 400];
    static int16_t abuf[AUDIO_RATE * 2];
    const uint64_t start_ns = SDL_GetTicksNS();
    uint64_t paused_ns = 0, pause_began = 0;
    uint64_t base_icount = 0;
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
                if (!paused) key_event(ev.key.scancode, down);   /* repeats re-send the make code */
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                int gx, gy;
                window_to_guest(ren, aspect, ev.motion.x, ev.motion.y, &gx, &gy);
                machine_mouse(&H.m, gx, gy, mouse_buttons, (int)ev.motion.xrel, (int)ev.motion.yrel);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
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
        if (pad || joy) {
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
            machine_joystick(&H.m, 1, axis, b);
        }

        /* Keep the machine's clock level with the wall clock. */
        if (!paused) {
            const uint64_t now_ns = SDL_GetTicksNS() - start_ns - paused_ns;
            uint64_t target = base_icount + (uint64_t)((double)now_ns * (double)ips / 1e9);
            /* Never more than 100 ms behind: after a stall, catch up gently
             * rather than racing. */
            if (target > H.m.cpu.icount + ips / 10) {
                base_icount += target - (H.m.cpu.icount + ips / 10);
                target = H.m.cpu.icount + ips / 10;
            }
            while (H.m.cpu.icount < target) {
                uint64_t until = H.m.cpu.icount + ips / 1000;  /* 1 ms slices */
                if (until > target) until = target;
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
            }
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

    recomp_shutdown(&H.m);
    machine_shutdown(&H.m);
#ifdef _WIN32
    if (H.midi_out) { midiOutReset(H.midi_out); midiOutClose(H.midi_out); }
#endif
    if (H.m.log) {
        recomp_report(&H.m, H.m.log);
        fclose(H.m.log);
    }
    SDL_Quit();
    return 0;
}
