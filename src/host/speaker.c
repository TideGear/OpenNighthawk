/* speaker.c - PIT counter 2 and port 61h, one PIT clock at a time (see speaker.h). */
#include "speaker.h"

#include <string.h>

#define PIT_HZ 1193182ull

void speaker_init(speaker_t *s, uint64_t ips, uint64_t rate, speaker_model model)
{
    memset(s, 0, sizeof *s);
    s->model = model;
    s->ips = ips;
    s->rate = rate;
    /* as the machine resets it: mode 3, count 0 (65536), gate and data off */
    s->mode = 3;
    s->latch = 65536;
    s->count = 65536;
    s->out = 1;
    s->avg_len = 1;
    s->place = 1;
    s->sample = 0;
    s->bound = PIT_HZ;
    s->live_zero = SPEAKER_RING;
}

/* The count a load takes: 1 is not a legal count in modes 2 and 3 (86Box,
 * measured on hardware: 2 and 65536). */
static int32_t load_value(const speaker_t *s)
{
    if (s->latch == 1 && s->mode == 2) return 2;
    if (s->latch == 1 && s->mode == 3) return 65536;
    return (int32_t)s->latch;
}

/* One clock edge of counter 2. Returns 1 when modes 2 and 3 reloaded. */
static int edge(speaker_t *s)
{
    if (s->strobe) { s->strobe = 0; s->out = 1; }
    if (s->pending) {                       /* the load clock does not count */
        s->pending = 0;
        s->count = load_value(s);
        s->odd = (uint8_t)(s->count & 1);
        s->low_half = 0;
        s->done_tc = 0;
        if (s->mode == 1) s->out = 0;
        return 0;
    }
    if (s->null) return 0;
    switch (s->mode) {
    case 0:                                 /* interrupt on terminal count */
        if (!s->gate) return 0;
        if (--s->count <= 0) {
            if (!s->done_tc) { s->out = 1; s->done_tc = 1; }
            s->count = 65536;
        }
        return 0;
    case 1:                                 /* hardware-retriggerable one-shot */
        if (--s->count <= 0) {
            if (!s->done_tc) { s->out = 1; s->done_tc = 1; }
            s->count = 65536;
        }
        return 0;
    case 4: case 5:                         /* software / hardware strobe */
        if (s->mode == 4 && !s->gate) return 0;
        if (--s->count <= 0) {
            if (!s->done_tc) { s->out = 0; s->strobe = 1; s->done_tc = 1; }
            s->count = 65536;
        }
        return 0;
    case 2:                                 /* rate generator: low for the last clock */
        if (!s->gate) return 0;
        if (--s->count == 1) s->out = 0;
        else if (s->count <= 0) { s->count = load_value(s); s->out = 1; return 1; }
        return 0;
    default:                                /* 3, square wave: a new count waits for the half-cycle's end */
        if (!s->gate) return 0;
        if (!s->low_half) {
            s->count -= s->odd ? 1 : 2;
            s->odd = 0;
            if (s->count <= 0) {
                s->out = 0; s->low_half = 1;
                s->count = load_value(s); s->odd = (uint8_t)(s->count & 1);
                return 1;
            }
        } else {
            s->count -= s->odd ? 3 : 2;
            s->odd = 0;
            if (s->count <= 0) {
                s->out = 1; s->low_half = 0;
                s->count = load_value(s); s->odd = (uint8_t)(s->count & 1);
                return 1;
            }
        }
        return 0;
    }
}

static int averaging(const speaker_t *s)
{
    return s->model == SPEAKER_REALSOUND && s->mode == 0 && s->gate && s->data && s->avg_len > 1;
}

/* Recompute the running sum over the last avg_len clocks. */
static void ring_resum(speaker_t *s)
{
    double sum = 0;
    for (unsigned i = 1; i <= s->avg_len; i++)
        sum += s->ring[(s->ring_at + SPEAKER_RING - i) % SPEAKER_RING];
    s->ring_sum = sum;
}

static void resync_place(speaker_t *s, uint64_t place)
{
    s->place = place;
    s->sample = place * s->rate / PIT_HZ;
    s->bound = (s->sample + 1) * PIT_HZ;
}

/* The cone's input over one complete PIT clock interval, `c` of it high.
 * It lands one clock later, on [place, place + 1). */
static void emit(speaker_t *s, double c)
{
    const float cf = (float)c;
    s->ring_sum += cf - s->ring[(s->ring_at + SPEAKER_RING - s->avg_len) % SPEAKER_RING];
    s->ring[s->ring_at] = cf;
    s->ring_at = (s->ring_at + 1) % SPEAKER_RING;
    if (cf != 0.0f) s->live_zero = 0;
    else if (s->live_zero < SPEAKER_RING && ++s->live_zero == SPEAKER_RING) s->ring_sum = 0.0;
    const double y = averaging(s) ? s->ring_sum / s->avg_len : c;

    const uint64_t lo = s->place * s->rate, hi = lo + s->rate;
    while (lo >= s->bound) { s->sample++; s->bound += PIT_HZ; }
    if (y != 0.0) {
        if (hi <= s->bound) s->acc[s->sample % SPEAKER_ACC] += y * (double)s->rate;
        else {
            const uint64_t w = s->bound - lo;
            s->acc[s->sample % SPEAKER_ACC] += y * (double)w;
            s->acc[(s->sample + 1) % SPEAKER_ACC] += y * (double)(s->rate - w);
        }
    }
    s->place++;
}

/* Run `n` clock edges with nothing to hear (data bit off, ring silent). */
static void run_quiet(speaker_t *s, uint64_t n)
{
    unsigned reloads = 0;
    while (n) {
        if (!s->pending && !s->strobe) {
            if (s->null) return;
            const int counting = s->gate || s->mode == 1 || s->mode == 5;
            if (!counting) return;                               /* frozen */
            if ((s->mode == 0 || s->mode == 1 || s->mode == 4 || s->mode == 5) && s->done_tc) return;
            if ((s->mode == 2 || s->mode == 3) && reloads >= 2) {
                /* steady: the state repeats every load_value clocks */
                const uint64_t p = (uint64_t)load_value(s);
                if (n > p) n -= (n / p - 1) * p;
                reloads = 0;
            }
        }
        reloads += (unsigned)edge(s);
        n--;
    }
}

/* Advance to clock interval k, `frac` of the way into it. */
static void advance_to(speaker_t *s, uint64_t k, double frac)
{
    if (k < s->tick || (k == s->tick && frac < s->pos)) return;
    while (s->tick < k) {
        if (!s->data && s->cov == 0.0 && s->live_zero >= SPEAKER_RING) {
            /* silent to k: the counter still runs, for when the data bit returns */
            emit(s, 0.0);                   /* close the interval under way */
            s->tick++; s->pos = 0.0;
            edge(s);
            if (s->tick < k) {
                run_quiet(s, k - s->tick);
                s->tick = k;
                resync_place(s, k + 1);
            }
            break;
        }
        const double level = (s->data && s->out) ? 1.0 : 0.0;
        emit(s, s->cov + level * (1.0 - s->pos));
        s->tick++; s->pos = 0.0; s->cov = 0.0;
        edge(s);
    }
    const double level = (s->data && s->out) ? 1.0 : 0.0;
    s->cov += level * (frac - s->pos);
    s->pos = frac;
}

static void advance_icount(speaker_t *s, uint64_t icount, uint64_t *k_out)
{
    const uint64_t q = icount / s->ips, r = icount % s->ips;
    const uint64_t num = r * PIT_HZ;
    const uint64_t k = q * PIT_HZ + num / s->ips;
    advance_to(s, k, (double)(num % s->ips) / (double)s->ips);
    if (k_out) *k_out = k;
}

void speaker_advance(speaker_t *s, uint64_t icount)
{
    advance_icount(s, icount, NULL);
}

void speaker_control(speaker_t *s, uint64_t icount, unsigned mode)
{
    advance_icount(s, icount, NULL);
    s->events++;
    mode &= 7;
    if (mode > 5) mode -= 4;               /* 6 and 7 are 2 and 3 */
    s->mode = (uint8_t)mode;
    s->null = 1;
    s->pending = 0;
    s->done_tc = 0;
    s->strobe = 0;
    s->low_half = 0;
    s->out = mode == 0 ? 0 : 1;
}

void speaker_count(speaker_t *s, uint64_t icount, unsigned reload)
{
    uint64_t k;
    advance_icount(s, icount, &k);
    s->events++;
    s->latch = reload ? reload : 65536u;
    switch (s->mode) {
    case 0:
        /* realsound: the carrier period, measured from the writes */
        if (s->gate && s->data) {
            const uint64_t interval = k - s->last_pwm_tick;
            if (s->last_pwm_tick && interval >= 8 && interval < SPEAKER_RING) {
                s->period_est = s->period_est > 0 ? s->period_est + ((double)interval - s->period_est) / 8.0
                                                  : (double)interval;
                const unsigned len = (unsigned)(s->period_est + 0.5);
                if (len != s->avg_len) { s->avg_len = len; ring_resum(s); }
            }
            s->last_pwm_tick = k;
        }
        s->out = 0; s->pending = 1; s->null = 0; s->done_tc = 0;
        break;
    case 4:
        s->pending = 1; s->null = 0; s->done_tc = 0;
        break;
    case 2: case 3:
        if (s->null) { s->pending = 1; s->null = 0; }
        break;                              /* else taken at the next reload */
    default:                                /* 1, 5: wait for a gate trigger */
        s->null = 0;
        break;
    }
}

void speaker_port61(speaker_t *s, uint64_t icount, unsigned bits)
{
    advance_icount(s, icount, NULL);
    s->events++;
    const uint8_t gate = (uint8_t)(bits & 1), data = (uint8_t)((bits >> 1) & 1);
    if (gate && !s->gate) {
        if (!s->null && (s->mode == 1 || s->mode == 2 || s->mode == 3 || s->mode == 5)) s->pending = 1;
    } else if (!gate && s->gate) {
        if (s->mode == 2 || s->mode == 3) s->out = 1;
    }
    s->gate = gate;
    s->data = data;
}

float speaker_take(speaker_t *s, uint64_t n)
{
    double *a = &s->acc[n % SPEAKER_ACC];
    const float v = (float)(*a / (double)PIT_HZ);
    *a = 0.0;
    return v;
}
