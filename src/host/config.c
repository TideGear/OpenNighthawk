/* config.c - f117a.ini (see config.h). */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The options a file may set: the value-taking ones, which of them are paths,
 * and the switches. */
static const char *const VALUED[] = {
    "data", "save", "engine", "ips", "scale", "midi", "log", "record", "replay", "time-us",
    "exit-after", "opl", "audio-queue-log", "audio-dump", "mt32-control", "mt32-pcm", "mt32-seed",
    "coverage", "fix", "roland", "mt32-roms", NULL };
static const char *const PATHS[] = {
    "data", "save", "log", "record", "replay", "audio-queue-log", "audio-dump", "mt32-control",
    "mt32-pcm", "mt32-roms", "coverage", NULL };
static const char *const SWITCHES[] = { "fullscreen", "no-aspect", "no-record", NULL };

static int in(const char *const *list, const char *s)
{
    for (int i = 0; list[i]; i++)
        if (!strcmp(list[i], s)) return 1;
    return 0;
}

static char *dup_n(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

static void trim(const char **a, const char **b)
{
    while (*a < *b && isspace((unsigned char)**a)) (*a)++;
    while (*b > *a && isspace((unsigned char)(*b)[-1])) (*b)--;
}

static int is_absolute(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

static int push(char ***v, int *n, int *cap, char *s)
{
    if (!s) return 0;
    if (*n == *cap) {
        int c = *cap ? *cap * 2 : 32;
        char **nv = (char **)realloc(*v, (size_t)c * sizeof **v);
        if (!nv) return 0;
        *v = nv;
        *cap = c;
    }
    (*v)[(*n)++] = s;
    return 1;
}

static int fail(char *err, size_t size, int line, const char *what, const char *key)
{
    if (err && size) snprintf(err, size, "f117a.ini line %d: %s%s%s", line, what, key ? " " : "", key ? key : "");
    return -1;
}

int config_merge(const char *text, const char *dir, int argc, char **argv,
                 char ***out_argv, char *err, size_t err_size)
{
    char **v = NULL;
    int n = 0, cap = 0, line = 0;
    if (!push(&v, &n, &cap, argv[0])) return fail(err, err_size, 0, "out of memory", NULL);
    for (const char *p = text ? text : ""; *p;) {
        const char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        const char *a = p, *b = end;
        p = *end ? end + 1 : end;
        line++;
        trim(&a, &b);
        if (a == b || *a == ';' || *a == '#' || *a == '[') continue;
        const char *eq = memchr(a, '=', (size_t)(b - a));
        if (!eq) return fail(err, err_size, line, "expected key = value", NULL);
        const char *ka = a, *kb = eq, *va = eq + 1, *vb = b;
        trim(&ka, &kb);
        trim(&va, &vb);
        if (vb - va >= 2 && *va == '"' && vb[-1] == '"') { va++; vb--; }
        char key[32];
        size_t kl = (size_t)(kb - ka);
        if (ka < kb && ka[0] == '-') { while (ka < kb && *ka == '-') ka++; kl = (size_t)(kb - ka); }
        if (!kl || kl >= sizeof key) return fail(err, err_size, line, "unknown option", NULL);
        for (size_t i = 0; i < kl; i++) key[i] = (char)tolower((unsigned char)ka[i]);
        key[kl] = 0;
        char opt[40];
        snprintf(opt, sizeof opt, "--%s", key);
        if (in(SWITCHES, key)) {
            char val[8] = { 0 };
            size_t vl = (size_t)(vb - va) < sizeof val - 1 ? (size_t)(vb - va) : sizeof val - 1;
            for (size_t i = 0; i < vl; i++) val[i] = (char)tolower((unsigned char)va[i]);
            int on;
            if (!strcmp(val, "yes") || !strcmp(val, "true") || !strcmp(val, "on") || !strcmp(val, "1")) on = 1;
            else if (!strcmp(val, "no") || !strcmp(val, "false") || !strcmp(val, "off") || !strcmp(val, "0")) on = 0;
            else return fail(err, err_size, line, "expected yes or no for", key);
            if (on && !push(&v, &n, &cap, dup_n(opt, strlen(opt)))) return fail(err, err_size, line, "out of memory", NULL);
            continue;
        }
        if (!in(VALUED, key)) return fail(err, err_size, line, "unknown option", key);
        if (va == vb) return fail(err, err_size, line, "no value for", key);
        if (!strcmp(key, "fix")) {                 /* a list: "D4, D5" or "D4 D5" */
            for (const char *s = va; s < vb;) {
                while (s < vb && (*s == ',' || isspace((unsigned char)*s))) s++;
                const char *e = s;
                while (e < vb && *e != ',' && !isspace((unsigned char)*e)) e++;
                if (e > s && (!push(&v, &n, &cap, dup_n(opt, strlen(opt))) ||
                              !push(&v, &n, &cap, dup_n(s, (size_t)(e - s)))))
                    return fail(err, err_size, line, "out of memory", NULL);
                s = e;
            }
            continue;
        }
        char *value;
        if (dir && *dir && in(PATHS, key) && !is_absolute(va)) {
            size_t dl = strlen(dir), vl = (size_t)(vb - va);
            value = (char *)malloc(dl + 1 + vl + 1);
            if (value) {
                memcpy(value, dir, dl);
                size_t at = dl;
                if (dl && dir[dl - 1] != '\\' && dir[dl - 1] != '/') value[at++] = '\\';
                memcpy(value + at, va, vl);
                value[at + vl] = 0;
            }
        } else value = dup_n(va, (size_t)(vb - va));
        if (!push(&v, &n, &cap, dup_n(opt, strlen(opt))) || !push(&v, &n, &cap, value))
            return fail(err, err_size, line, "out of memory", NULL);
    }
    if (!push(&v, &n, &cap, (char *)CONFIG_CLI_MARK)) return fail(err, err_size, 0, "out of memory", NULL);
    for (int i = 1; i < argc; i++)
        if (!push(&v, &n, &cap, argv[i])) return fail(err, err_size, 0, "out of memory", NULL);
    *out_argv = v;
    return n;
}

const char *config_path(int argc, char **argv, const char *base_dir, int *explicit_)
{
    *explicit_ = 0;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--config")) { *explicit_ = 1; return argv[i + 1]; }
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--no-config")) return NULL;
    if (!base_dir) return NULL;
    static char path[1024];
    snprintf(path, sizeof path, "%sf117a.ini", base_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fclose(f);
    return path;
}

char *config_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    size_t n = 0, cap = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 2 > cap) {
            cap = cap ? cap * 2 : 4096;
            char *nb = (char *)realloc(buf, cap);
            if (!nb) { free(buf); fclose(f); return NULL; }
            buf = nb;
        }
        buf[n++] = (char)c;
    }
    fclose(f);
    if (!buf) buf = (char *)calloc(1, 1);
    else buf[n] = 0;
    /* a UTF-8 byte-order mark, as Notepad may write */
    if (buf && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        memmove(buf, buf + 3, n - 2);
    return buf;
}
