/* inputlog.c - see inputlog.h. */
#include "inputlog.h"

#include <stdlib.h>
#include <string.h>

void inputlog_header(FILE *f, uint64_t ips, uint64_t time_us)
{
    fprintf(f, "# f117r-input ips=%llu time_us=%llu\n",
            (unsigned long long)ips, (unsigned long long)time_us);
}

void inputlog_write(FILE *f, const machine_input *in)
{
    switch (in->type) {
    case INPUT_KEY:
        fprintf(f, "K %llu %02X\n", (unsigned long long)in->at, in->byte);
        break;
    case INPUT_MOUSE:
        fprintf(f, "M %llu %d %d %u %d %d\n", (unsigned long long)in->at,
                in->x, in->y, in->buttons, in->dx, in->dy);
        break;
    case INPUT_JOY:
        fprintf(f, "J %llu %u %u %u %u %u %u\n", (unsigned long long)in->at, in->present,
                in->axis[0], in->axis[1], in->axis[2], in->axis[3], in->buttons);
        break;
    default: break;
    }
}

void inputlog_read_header(const char *path, uint64_t *ips, uint64_t *time_us)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    if (fgets(line, sizeof line, f) && !strncmp(line, "# f117r-input", 13)) {
        const char *p = strstr(line, "ips=");
        if (p && ips) *ips = strtoull(p + 4, NULL, 10);
        p = strstr(line, "time_us=");
        if (p && time_us) *time_us = strtoull(p + 8, NULL, 10);
    }
    fclose(f);
}

struct inputlog_reader {
    FILE *f;
    machine_input next;
    int have;              /* `next` holds an input not yet queued */
    int eof;
};

static int parse(const char *line, machine_input *in)
{
    memset(in, 0, sizeof *in);
    unsigned long long at;
    int a, b, c2, d, e;
    unsigned u0, u1, u2, u3, u4, u5;
    if (line[0] == 'K' && sscanf(line + 1, "%llu %x", &at, &u0) == 2) {
        in->at = at; in->type = INPUT_KEY; in->byte = (uint8_t)u0;
        return 1;
    }
    if (line[0] == 'M' && sscanf(line + 1, "%llu %d %d %d %d %d", &at, &a, &b, &c2, &d, &e) == 6) {
        in->at = at; in->type = INPUT_MOUSE; in->x = (int16_t)a; in->y = (int16_t)b;
        in->buttons = (uint16_t)c2; in->dx = (int16_t)d; in->dy = (int16_t)e;
        return 1;
    }
    if (line[0] == 'J' && sscanf(line + 1, "%llu %u %u %u %u %u %u", &at, &u0, &u1, &u2, &u3, &u4, &u5) == 7) {
        in->at = at; in->type = INPUT_JOY; in->present = (uint8_t)u0;
        in->axis[0] = (uint16_t)u1; in->axis[1] = (uint16_t)u2;
        in->axis[2] = (uint16_t)u3; in->axis[3] = (uint16_t)u4;
        in->buttons = (uint16_t)u5;
        return 1;
    }
    return 0;
}

static void advance(inputlog_reader *r)
{
    char line[256];
    r->have = 0;
    while (!r->eof) {
        if (!fgets(line, sizeof line, r->f)) { r->eof = 1; break; }
        if (parse(line, &r->next)) { r->have = 1; break; }
    }
}

inputlog_reader *inputlog_open(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    inputlog_reader *r = (inputlog_reader *)calloc(1, sizeof *r);
    if (!r) { fclose(f); return NULL; }
    r->f = f;
    advance(r);
    return r;
}

void inputlog_feed(inputlog_reader *r, machine_t *m, uint64_t horizon)
{
    while (r && r->have && r->next.at < horizon) {
        machine_input_at(m, &r->next);
        advance(r);
    }
}

int inputlog_done(const inputlog_reader *r)
{
    return !r || !r->have;
}

void inputlog_close(inputlog_reader *r)
{
    if (!r) return;
    fclose(r->f);
    free(r);
}
