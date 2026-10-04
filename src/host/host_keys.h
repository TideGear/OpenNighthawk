#ifndef F117R_HOST_KEYS_H
#define F117R_HOST_KEYS_H
#include <SDL3/SDL_scancode.h>
#include <stdint.h>

static inline unsigned pc_scancode(SDL_Scancode s)
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
#endif
