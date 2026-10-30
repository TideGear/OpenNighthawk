/* remap.c - keyboard remapping (see remap.h). */
#include "remap.h"

#include <SDL3/SDL_keyboard.h>

#include <ctype.h>
#include <string.h>

#define MAX_PAIRS 64

static struct { SDL_Scancode from, to; } g_pairs[MAX_PAIRS];
static int g_count;

static SDL_Scancode named(const char *a, const char *b)
{
    char name[48];
    size_t n = (size_t)(b - a);
    while (n && isspace((unsigned char)*a)) { a++; n--; }
    while (n && isspace((unsigned char)a[n - 1])) n--;
    if (!n || n >= sizeof name) return SDL_SCANCODE_UNKNOWN;
    memcpy(name, a, n);
    name[n] = 0;
    return SDL_GetScancodeFromName(name);
}

int remap_add(const char *spec)
{
    int first = g_count, added = 0;
    for (const char *p = spec; *p;) {
        while (*p == ',' || *p == ';') p++;
        const char *end = p;
        while (*end && *end != ',' && *end != ';') end++;
        if (end > p) {
            const char *colon = memchr(p, ':', (size_t)(end - p));
            const SDL_Scancode from = colon ? named(p, colon) : SDL_SCANCODE_UNKNOWN;
            const SDL_Scancode to = colon ? named(colon + 1, end) : SDL_SCANCODE_UNKNOWN;
            if (from == SDL_SCANCODE_UNKNOWN || to == SDL_SCANCODE_UNKNOWN || g_count == MAX_PAIRS) {
                g_count = first;
                return -1;
            }
            int at = -1;
            for (int i = 0; i < g_count; i++) if (g_pairs[i].from == from) at = i;
            if (at < 0) at = g_count++;
            g_pairs[at].from = from;
            g_pairs[at].to = to;
            added++;
        }
        p = end;
    }
    return added;
}

SDL_Scancode remap_apply(SDL_Scancode physical)
{
    for (int i = 0; i < g_count; i++)
        if (g_pairs[i].from == physical) return g_pairs[i].to;
    return physical;
}

void remap_clear(void)
{
    g_count = 0;
}
