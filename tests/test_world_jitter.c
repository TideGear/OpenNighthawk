/* Measure interpolation quantization in projected outlines and dot lights.
 * Usage: test_world_jitter LOG [N]. Each triple is at 1/8-step intervals
 * within one skeleton half. Only lines with matched exact snapshots count;
 * this isolates rounding jitter rather than camera acceleration or cuts. */
#include "drawfeed.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t memory[MEM_SIZE];
static drawfeed feed;
static drawlive live;
static int g_n = 9;

static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t len = 0;
    if (!*buf) { *cap = 1 << 16; *buf = (char *)malloc(*cap); }
    for (;;) {
        if (!fgets(*buf + len, (int)(*cap - len), f)) return len ? *buf : NULL;
        len += strlen(*buf + len);
        if (len && (*buf)[len - 1] == '\n') return *buf;
        *cap *= 2;
        *buf = (char *)realloc(*buf, *cap);
    }
}

static int parse_values(char kind, char *p, int32_t *v, int cap)
{
    int n = 0;
    if (kind == 'V') {
        for (int q = 0; q < 8; q++) { char *e; v[n++] = (int32_t)strtol(p, &e, q < 6 ? 10 : 16); p = e; }
        return n;
    }
    for (;;) {
        char *e;
        const long x = strtol(p, &e, 10);
        if (e == p || n == cap) break;
        v[n++] = (int32_t)x;
        p = e;
    }
    return n;
}


typedef struct {double old[4],fine[4];int valid,dot; } sample;
static sample samples[3][8192];
static struct {size_t n;double old_sq,new_sq,old_max,new_max;} acc[3];
static void sample_pair(const drawlive_step *a,const drawlive_step *b)
{
    interp_list list={0};int sb;
    for(int half=0;half<2;half++) {
        memset(samples,0,sizeof samples);
        int counts[3]={0};
        for(int step=0;step<3;step++) {
            const double t=half*0.5+0.125*(step+1);
            interp_inbetween_ex(a->parsed,b->parsed,b->pair,t,INTERP_VERTICES,&list,&sb,NULL);
            for(int k=0;k<list.n;k++) {
                const interp_rec *r=&list.rec[k];
                if(r->kind!='L')continue;
                int idx=counts[step]++;
                if(idx>=8192 || (r->n!=27 && r->n!=28))continue;
                sample *p=&samples[step][idx];
                int32_t l[10];memcpy(l,r->v,sizeof l);const int32_t *g=r->v+10;
                for(int e=0;e<2;e++){l[1+2*e]=g[8+6*e]+g[3];l[2+2*e]=g[9+6*e]+g[4];}
                if(!hires_outline_points(g,r->n-10,l,10,p->fine))continue;
                p->valid=1;p->dot=g[1]==g[2];
                for(int axis=0;axis<4;axis++)p->old[axis]=r->v[1+axis]+0.5;
            }
        }
        int n=counts[0];if(counts[1]!=n || counts[2]!=n)continue;
        if(n>8192)n=8192;
        for(int k=0;k<n;k++) {
            if(!samples[0][k].valid || !samples[1][k].valid || !samples[2][k].valid)continue;
            const int cls=samples[1][k].dot;
            for(int axis=0;axis<(cls?2:4);axis++) {
                const double before=(samples[2][k].old[axis]-2*samples[1][k].old[axis]+samples[0][k].old[axis])*g_n;
                const double after=(samples[2][k].fine[axis]-2*samples[1][k].fine[axis]+samples[0][k].fine[axis])*g_n;
                acc[cls].n++;acc[cls].old_sq+=before*before;acc[cls].new_sq+=after*after;
                if(fabs(before)>acc[cls].old_max)acc[cls].old_max=fabs(before);
                if(fabs(after)>acc[cls].new_max)acc[cls].new_max=fabs(after);
            }
        }
    }
    /* The polygon snapshots use the same projection fractions. Measure the
     * full-precision projection separately, on their interpolated negative axes. */
    for(int half=0;half<2;half++) {
        memset(samples,0,sizeof samples);int counts[3]={0};
        for(int step=0;step<3;step++) {
            interp_inbetween_ex(a->parsed,b->parsed,b->pair,half*0.5+0.125*(step+1),INTERP_VERTICES,&list,&sb,NULL);
            for(int k=0;k<list.n;k++) {
                const interp_rec *r=&list.rec[k];if(r->kind!='G')continue;
                int idx=counts[step]++;if(idx>=8192 || r->n<17 || (r->n>=18 && r->v[17]))continue;
                sample *p=&samples[step][idx];p->valid=1;
                for(int end=0;end<2;end++) {
                    const int at=5+6*end;const int32_t *g=r->v;
                    int zhi=(int16_t)(g[at+2]>>16);
                    if(g[at+5] || zhi<1){p->valid=0;break;}
                    const int64_t den=zhi>=256?zhi:(((uint32_t)g[at+2]>>8)&65535)>>1;
                    if(!den){p->valid=0;break;}
                    for(int axis=0;axis<2;axis++) {
                        const int64_t num=(int64_t)g[at+axis]>>(zhi>=256?8:1);
                        const int pos=2*end+axis;
                        if(num>=0){p->old[pos]=p->fine[pos]=NAN;continue;}
                        const int64_t rem=num%den;
                        p->old[pos]=g[at+3+axis]+(double)((rem+den)%den)/den;
                        p->fine[pos]=(double)g[at+axis]*256.0/g[at+2];
                    }
                }
            }
        }
        int n=counts[0];if(counts[1]!=n || counts[2]!=n)continue;if(n>8192)n=8192;
        for(int k=0;k<n;k++) {
            if(!samples[0][k].valid || !samples[1][k].valid || !samples[2][k].valid)continue;
            for(int axis=0;axis<4;axis++) {
                const double before=(samples[2][k].old[axis]-2*samples[1][k].old[axis]+samples[0][k].old[axis])*g_n;
                const double after=(samples[2][k].fine[axis]-2*samples[1][k].fine[axis]+samples[0][k].fine[axis])*g_n;
                if(!isfinite(before) || !isfinite(after))continue;
                acc[2].n++;acc[2].old_sq+=before*before;acc[2].new_sq+=after*after;
                if(fabs(before)>acc[2].old_max)acc[2].old_max=fabs(before);
                if(fabs(after)>acc[2].new_max)acc[2].new_max=fabs(after);
            }
        }
    }
    interp_list_free(&list);
}
int main(int argc,char **argv)
{
    if(argc<2 || argc>3)return 2;
    const char *path=argv[1];if(argc==3)g_n=atoi(argv[2]);
    if(g_n<1 || g_n>12)return 2;
    FILE *f=fopen(path,"r");if(!f)return 2;
    char *buf=NULL;static uint8_t *ydump[4096];
    static int32_t v[3+3*65536+16];
    size_t cap = 0;
    char *line;
    int phases = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == 'P') { phases++; continue; }
        if (k != 'Y' || phases < 1 || phases >= 4096) continue;
        char *p = line + 1;
        strtoull(p, &p, 10);
        const int n = parse_values(k, p, v, (int)(sizeof v / sizeof v[0]));
        if (n >= 2 + 64000) {
            ydump[phases - 1] = (uint8_t *)malloc(64000);
            for (int a = 0; a < 64000; a++) ydump[phases - 1][a] = (uint8_t)v[2 + a];
        }
    }
    rewind(f);
    drawfeed_init(&feed, memory);
    drawlive_init(&live);

    live.interp = 1;
    int phase = -1, deliver = 0;
    uint64_t last_seq = ~0ull;
    int pairs = 0;
    while ((line = read_line(f, &buf, &cap))) {
        const char k = line[0];
        if (k == '\n' || k == ' ' || !k) continue;
        char *p = line + 1;
        const uint64_t icount = strtoull(p, &p, 10);
        if (k == 'P') {
            phase++;
            if (phase < 4096 && ydump[phase]) memcpy(memory + 0xA0000, ydump[phase], 64000);
            feed.obs.frame_phase(feed.obs.user, icount);
            deliver = 0;
            drawlive_update(&live, &feed);
            const drawlive_step *a, *b;
            drawlive_steps(&live, &a, &b);
            if (a && b && b->pair && live.picture_seq != last_seq) { last_seq = live.picture_seq; sample_pair(a, b); pairs++; }
            continue;
        }
        if (phase < 0) continue;
        if (k == 'Z') deliver = feed.want_seed;
        if ((k == 'Z' || k == 'Y' || k == 'J') && !deliver) continue;
        const int n = parse_values(k, p, v, (int)(sizeof v / sizeof v[0]));
        if (k == 'V') feed.obs.vertex(feed.obs.user, icount, v, v + 3, v[5], (uint16_t)v[6], (uint16_t)v[7]);
        else feed.obs.prim(feed.obs.user, icount, k, v, n);
    }
    fclose(f);

    printf("%d logic pairs; fine pixels N=%d, within-pair second differences, samples 1/8 step apart\n",pairs,g_n);
    for(int k=0;k<3;k++) {
        if(!acc[k].n)continue;
        printf("%s: %zu components; before rms %.6f max %.6f; after rms %.6f max %.6f\n",k==2?"negative geometry axes":k?"dot markers":"outlines",acc[k].n,
            sqrt(acc[k].old_sq/acc[k].n),acc[k].old_max,sqrt(acc[k].new_sq/acc[k].n),acc[k].new_max);
    }
    return 0;
}
