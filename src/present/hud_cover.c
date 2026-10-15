/* hud_cover.c - see hud_cover.h. */
#include "hud_cover.h"

#include <string.h>

static uint64_t mix(uint64_t h, uint64_t v)
{
    h ^= v & 0xFFFF;
    h *= 0x100000001B3ULL;
    h ^= (v >> 16) & 0xFFFF;
    h *= 0x100000001B3ULL;
    return h;
}

#define SIG0 0xCBF29CE484222325ULL

static uint64_t sig_prim(const hud_prim *p)
{
    uint64_t h = mix(SIG0, (uint64_t)p->kind + 1);
    switch (p->kind) {
    case HP_K: h = mix(h, p->colour); break;
    case HP_N:
        h = mix(h, (uint16_t)p->x0); h = mix(h, (uint16_t)p->y0); h = mix(h, (uint16_t)p->x1); h = mix(h, (uint16_t)p->y1);
        h = mix(h, (uint32_t)p->origin);
        break;
    case HP_T:
        h = mix(h, p->entry);
        for (int k = 1; k < 11; k++) h = mix(h, p->blk[k]);          /* not the page word: the pass sets it itself */
        h = mix(h, p->ntext);
        for (int k = 0; k < p->ntext; k++) h = mix(h, p->text[k]);
        break;
    case HP_H: h = mix(h, (uint16_t)p->si); h = mix(h, (uint16_t)p->bx); h = mix(h, (uint16_t)p->dl); h = mix(h, (uint16_t)p->cl); break;
    case HP_S18: break;
    case HP_S:
        h = mix(h, p->entry);
        for (int k = 0; k < 8; k++) h = mix(h, p->blk[k]);
        break;
    }
    return h;
}

/* the same, from the observer's record values: N x0 y0 x1 y1 driver page origin; T entry page block[11]
 * count chars...; H si bx dl cl colour x lo hi page; S entry ... ; K colour. -1 for a record no pass makes. */
static int sig_rec(char kind, const int32_t *v, int n, int *pk, uint64_t *sig)
{
    uint64_t h;
    switch (kind) {
    case 'K':
        if (n < 1) return 0;
        h = mix(mix(SIG0, HP_K + 1), (uint32_t)v[0] & 0xFF);
        *pk = HP_K;
        break;
    case 'N':
        if (n < 7) return 0;
        h = mix(SIG0, HP_N + 1);
        for (int k = 0; k < 4; k++) h = mix(h, (uint16_t)v[k]);
        h = mix(h, (uint32_t)v[6]);
        *pk = HP_N;
        break;
    case 'T': {
        if (n < 14) return 0;
        const int cnt = v[13];
        if (cnt < 0 || cnt > HUD_TEXT_MAX || 14 + cnt > n) return 0;
        h = mix(mix(SIG0, HP_T + 1), (uint32_t)v[0]);
        for (int k = 1; k < 11; k++) h = mix(h, (uint16_t)v[2 + k]);
        h = mix(h, (uint32_t)cnt);
        for (int k = 0; k < cnt; k++) h = mix(h, (uint32_t)v[14 + k] & 0xFF);
        *pk = HP_T;
        break;
    }
    case 'H':
        if (n < 8) return 0;
        h = mix(SIG0, HP_H + 1);
        for (int k = 0; k < 4; k++) h = mix(h, (uint16_t)v[k]);
        *pk = HP_H;
        break;
    case 'S':
        if (n >= 1 && v[0] == 18) {
            h = mix(SIG0, HP_S18 + 1);
            *pk = HP_S18;
            break;
        }
        if (n < 10 || (v[0] != 71 && v[0] != 73)) return 0;
        h = mix(mix(SIG0, HP_S + 1), (uint32_t)v[0]);
        for (int k = 0; k < 8; k++) h = mix(h, (uint16_t)v[2 + k]);
        *pk = HP_S;
        break;
    default: return 0;
    }
    *sig = h;
    return 1;
}

void hud_cover_begin(hud_cover *c, const hud_exact_list *ex)
{
    c->n = ex->n;
    c->at = 0;
    c->active = ex->n > 0;
    for (int i = 0; i < ex->n; i++) {
        c->item[i].kind = ex->p[i].kind;
        c->item[i].keep = ex->p[i].keep;
        c->item[i].item = ex->p[i].item;
        c->item[i].sig = sig_prim(&ex->p[i]);
    }
}

int hud_cover_step(hud_cover *c, char kind, const int32_t *v, int n)
{
    if (!c->active) return HC_NONE;
    int pk;
    uint64_t sig;
    if (!sig_rec(kind, v, n, &pk, &sig)) return HC_NONE;
    if (c->at >= c->n) { c->active = 0; return HC_NONE; }
    if (c->item[c->at].sig != sig) { c->active = 0; return HC_MISMATCH; }
    const int keep = c->item[c->at].keep;
    c->last = c->item[c->at].item;
    c->at++;
    if (c->at >= c->n) c->active = 0;
    return keep ? HC_KEPT : HC_REPLACED;
}

void hud_pcover_begin(hud_cover *c, const hud_exact_list *ex)
{
    hud_cover_begin(c, ex);
    c->missed = 0;
}

int hud_pcover_step(hud_cover *c, char kind, const int32_t *v, int n)
{
    if (!c->active) return HC_NONE;
    int pk;
    uint64_t sig;
    if (!sig_rec(kind, v, n, &pk, &sig)) return HC_NONE;
    /* the next item, or (for a line, a text or a sprite) a later one, the items between then left to the original */
    int j = c->at;
    if (j < c->n && c->item[j].sig != sig) {
        j = -1;
        if (pk != HP_K)
            for (int q = c->at + 1; q < c->n; q++)
                if (c->item[q].sig == sig) { j = q; break; }
    }
    if (j < 0 || j >= c->n) return HC_NONE;
    for (int q = c->at; q < j; q++) c->missed += c->item[q].kind != HP_K;
    const int keep = c->item[j].keep;
    c->last = c->item[j].item;
    c->at = j + 1;
    if (c->at >= c->n) c->active = 0;
    return keep ? HC_KEPT : HC_REPLACED;
}

int hud_pcover_end(hud_cover *c)
{
    int missed = c->missed;
    if (c->active)
        for (int q = c->at; q < c->n; q++) missed += c->item[q].kind != HP_K;
    c->active = 0;
    c->missed = 0;
    c->n = 0;
    return missed;
}

void hud_art_note(hud_ov *o, const int32_t *v, int n)
{
    if (n < 14 || (v[0] != 18 && v[0] != 71 && v[0] != 73)) return;
    const int w = (int16_t)v[2 + 6], h = (int16_t)v[2 + 7];
    if (w <= 0 || h <= 0 || w * h > HUD_ART_MAX || n - 14 != w * h) return;
    int slot = -1;
    for (int k = 0; k < HUD_ARTS; k++)
        if (o->art[k].valid && o->art[k].entry == v[0] && o->art[k].seg == (v[2] & 0xFFFF) && o->art[k].sx == (v[3] & 0xFFFF) &&
            o->art[k].sy == (v[4] & 0xFFFF) && o->art[k].w == w && o->art[k].h == h) { slot = k; break; }
    if (slot < 0)
        for (int k = 0; k < HUD_ARTS; k++) if (!o->art[k].valid) { slot = k; break; }
    if (slot < 0) return;
    hud_art *a = &o->art[slot];
    int changed = !a->valid;
    for (int k = 0; k < w * h; k++) {
        if (a->valid && a->px[k] != (uint8_t)v[14 + k]) changed = 1;
        a->px[k] = (uint8_t)v[14 + k];
    }
    if (a->valid && changed) o->art_changed++;
    a->valid = 1; a->entry = v[0]; a->seg = v[2] & 0xFFFF; a->sx = v[3] & 0xFFFF; a->sy = v[4] & 0xFFFF; a->w = w; a->h = h;
    for (int k = 0; k < 4; k++) a->clip[k] = (int16_t)v[2 + 8 + k];
}

const hud_art *hud_art_find(const hud_ov *o, int entry, int seg, int sx, int sy, int w, int h)
{
    for (int k = 0; k < HUD_ARTS; k++)
        if (o->art[k].valid && o->art[k].entry == entry && o->art[k].seg == seg && o->art[k].sx == sx && o->art[k].sy == sy &&
            o->art[k].w == w && o->art[k].h == h) return &o->art[k];
    return NULL;
}

void hud_fonts_note(hud_ov *o, const int32_t *v, int n)
{
    if (n < 14) return;
    const int nchars = v[13];
    if (nchars < 0 || 14 + nchars > n) return;
    const int *rest = (const int *)(v + 14 + nchars);
    const int nrest = n - 14 - nchars;
    if (nrest < 8 || !rest[0]) return;
    const int idx = v[2 + 6];
    if (idx < 0 || idx >= HUD_FONTS) return;
    hud_font f;
    f.first = rest[1]; f.last = rest[2]; f.shift = rest[3]; f.fixed = rest[4];
    f.height = rest[5]; f.spacing = rest[6]; f.extra = rest[7];
    f.count = f.last - f.first + 1;
    if (f.count < 0 || f.count > 256 || 9 + f.count > nrest) return;
    for (int k = 0; k < f.count; k++) f.widths[k] = (uint8_t)rest[8 + k];
    f.nglyph = rest[8 + f.count];
    if (f.nglyph > nrest - 9 - f.count) f.nglyph = nrest - 9 - f.count;
    if (f.nglyph < 0) f.nglyph = 0;
    if (f.nglyph > (int)sizeof f.glyph) f.nglyph = (int)sizeof f.glyph;
    for (int k = 0; k < f.nglyph; k++) f.glyph[k] = (uint8_t)rest[9 + f.count + k];
    f.valid = 1;
    o->font[idx] = f;
}
