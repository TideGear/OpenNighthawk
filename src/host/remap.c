/* remap.c - keyboard remapping (see remap.h). */
#include "remap.h"

#include <SDL3/SDL_keyboard.h>

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PAIRS SDL_SCANCODE_COUNT

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
    if(!strncmp(name,"Scan",4)) {
        char *end;long id=strtol(name+4,&end,10);
        if(name[4] && !*end && id>0 && id<SDL_SCANCODE_COUNT && *SDL_GetScancodeName((SDL_Scancode)id))return (SDL_Scancode)id;
    }
    return SDL_GetScancodeFromName(name);
}

int remap_add(const char *spec)
{
    int first = g_count, added = 0;
    unsigned char backup[sizeof g_pairs];memcpy(backup,g_pairs,sizeof backup);
    for (const char *p = spec; *p;) {
        while (*p == ',' || *p == ';') p++;
        const char *end = p;
        while (*end && *end != ',' && *end != ';') end++;
        if (end > p) {
            const char *colon = memchr(p, ':', (size_t)(end - p));
            const SDL_Scancode from = colon ? named(p, colon) : SDL_SCANCODE_UNKNOWN;
            const SDL_Scancode to = colon ? named(colon + 1, end) : SDL_SCANCODE_UNKNOWN;
            if (from == SDL_SCANCODE_UNKNOWN || to == SDL_SCANCODE_UNKNOWN || !remap_set(from,to)) {
                g_count = first;
                memcpy(g_pairs,backup,sizeof backup);
                return -1;
            }
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

void remap_remove(SDL_Scancode physical)
{
    for(int i=0;i<g_count;i++) if(g_pairs[i].from==physical) {
        memmove(g_pairs+i,g_pairs+i+1,(size_t)(g_count-i-1)*sizeof *g_pairs);--g_count;return;
    }
}

int remap_set(SDL_Scancode physical, SDL_Scancode target)
{
    if(physical<=SDL_SCANCODE_UNKNOWN || physical>=SDL_SCANCODE_COUNT || target<=SDL_SCANCODE_UNKNOWN || target>=SDL_SCANCODE_COUNT)return 0;
    if(physical==target) {remap_remove(physical);return 1;}
    int at=-1;
    for(int i=0;i<g_count;i++)if(g_pairs[i].from==physical)at=i;
    if(at<0) {if(g_count==MAX_PAIRS)return 0;at=g_count++;}
    g_pairs[at].from=physical;g_pairs[at].to=target;return 1;
}

char *remap_export(void)
{
    const size_t capacity=(size_t)MAX_PAIRS*100+1;
    char *out=calloc(capacity,1);
    if(!out)return NULL;
    for(int i=0;i<g_count;i++) if(g_pairs[i].from!=g_pairs[i].to) {
        const size_t used=strlen(out);
        const char *from=SDL_GetScancodeName(g_pairs[i].from), *to=SDL_GetScancodeName(g_pairs[i].to);
        char a[24],b[24];
        if(strpbrk(from,",;:")) {snprintf(a,sizeof a,"Scan%d",g_pairs[i].from);from=a;}
        if(strpbrk(to,",;:")) {snprintf(b,sizeof b,"Scan%d",g_pairs[i].to);to=b;}
        snprintf(out+used,capacity-used,"%s%s:%s",used ? "," : "",from,to);
    }
    return out;
}
