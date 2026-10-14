/* A native BIOS callback can cross a slice without an extra guest instruction.
 * The matched-overrun diagnostic must distinguish that from a real extra NOP. */
#include "machine.h"
#include "recomp_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static machine_t m;
static int failed;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);failed++;} } while(0)
static int one_int(machine_t *p) { cpu_step(&p->cpu);return 1; }
static int extra_nop(machine_t *p) { cpu_step(&p->cpu);cpu_step(&p->cpu);return 1; }

int main(void)
{
    static const uint8_t code[]={0xCD,0x10,0x90,0x90,0xCD,0x10,0x90,0x90};
    m.mem=calloc(1,MEM_SIZE);if(!m.mem)return 1;
    cpu_init(&m.cpu,m.mem);m.cpu.user=&m;m.cpu.int_hook=dos_int_hook;
    m.engine=ENGINE_RECOMP;m.ips=MACHINE_DEFAULT_IPS;m.iret_seg=0x60;
    /* Original, unhooked INT 10h vector; the actual native service runs. */
    m.mem[0x40]=0x40;m.mem[0x42]=0x60;
    memcpy(m.mem+0x60000,code,sizeof code);
    recomp_init(&m);
    const recomp_override good={"matched","BOUND.OVL",0,0,0,one_int,"native one INT",1};
    const recomp_override bad={"matched","BOUND.OVL",0,0,4,extra_nop,"deliberate extra NOP",1};
    CHECK(recomp_override_add(&good)>=0 && recomp_override_add(&bad)>=0);
    recomp_module_load(NULL,&m,"BOUND.OVL",code,sizeof code,MODLOAD_OVERLAY,0x6000,0x6000);
    for(unsigned room=1;room<=3;room++) {
        m.cpu.seg[S_CS]=0x6000;m.cpu.ip=0;m.cpu.r[R_AX]=0x0F00;
        m.cpu.icount=1000;m.cpu.stop_at=1000+room;m.cpu.charged=0;m.cpu.io_cross=0;
        CHECK(recomp_override_step(&m)==1);
        CHECK(m.cpu.icount==1003 && m.cpu.charged==2 && m.cpu.ip==2);
    }
    m.cpu.seg[S_CS]=0x6000;m.cpu.ip=4;m.cpu.r[R_AX]=0x0F00;
    m.cpu.icount=1000;m.cpu.stop_at=1001;m.cpu.charged=0;m.cpu.io_cross=0;
    CHECK(recomp_override_step(&m)==1);
    CHECK(m.cpu.icount==1004 && m.cpu.ip==7);
    FILE *f=tmpfile();CHECK(f!=NULL);
    if(f) {
        char report[8192];recomp_report(&m,f);rewind(f);
        const size_t n=fread(report,1,sizeof report-1,f);report[n]=0;fclose(f);
        CHECK(strstr(report,"OVERRUN BOUND.OVL 0000:0000")==NULL);
        CHECK(strstr(report,"OVERRUN BOUND.OVL 0000:0004")!=NULL);
    }
    recomp_shutdown(&m);free(m.mem);
    if(!failed)puts("native boundary: BIOS clock unchanged, atomic callback allowed, extra instruction caught");
    return failed?1:0;
}
