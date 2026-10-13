/* Exercise the resident graphics driver's rare presentation branches with
 * staged text/fonts, sprites and tick tables. Oracle: original instructions
 * from the owner's generated MGRAPHIC module, not another replay rule. */
#include "drawlist.h"
#include "recomp_gen.h"
#include <stdio.h>
#include <string.h>

enum { CS = 0x019C, DS = 0x4000, SS = 0x5000, PAGE = 0x6000, SRC = 0x7000,
       BLOCK = 0x100, STRING = 0x200, FONT = 0x200, SP = 0xFF00 };
static uint8_t mem[MEM_SIZE], before[65536];
static cpu_t cpu;
static drawlist d;
static const rc_module *module;
static unsigned cases;

static void w(int seg, int off, int value)
{
    const unsigned at = 16u * seg + (uint16_t)off;
    mem[at] = (uint8_t)value; mem[at + 1] = (uint8_t)(value >> 8);
}
static void begin(int ip)
{
    memset(mem,0,sizeof mem);memcpy(mem,module->image,module->size);
    for(unsigned a=0;a<sizeof before;a++)before[a]=(uint8_t)(a*37+(a>>8));
    memcpy(mem+16*PAGE,before,sizeof before);
    cpu_init(&cpu,mem);cpu.model=CPU_80286;cpu.flags=2;
    cpu.seg[S_CS]=CS;cpu.seg[S_DS]=DS;cpu.seg[S_SS]=SS;
    cpu.r[R_SP]=SP;cpu.r[R_BP]=BLOCK;cpu.ip=(uint16_t)ip;
    w(SS,SP,0xFFF0);w(SS,SP+2,0x3000);
    for(int y=0;y<200;y++)w(CS,4+2*y,320*y);
    w(CS,0x194,PAGE);w(CS,0x196,0);w(CS,0x787,PAGE);
    drawlist_reset(&d);drawlist_seed(&d,'Z',PAGE,0,before,sizeof before);
}
static int finish(const char *what, int detail)
{
    unsigned steps=0;
    while(!(cpu.seg[S_CS]==0x3000 && cpu.ip==0xFFF0) && steps++<200000)cpu_step(&cpu);
    const drawlist_page *p=drawlist_get(&d,PAGE);
    if(steps>=200000 || !p || memcmp(p->b,mem+16*PAGE,sizeof before) || d.ncopied) {
        unsigned at=0;
        if(p)while(at<sizeof before && p->b[at]==mem[16*PAGE+at])at++;
        fprintf(stderr,"%s case %d: first difference %04X (%u/%u), stopped %04X:%04X after %u instructions\n",
                what,detail,at,p && at<sizeof before?p->b[at]:0,
                at<sizeof before?mem[16*PAGE+at]:0,cpu.seg[S_CS],cpu.ip,steps);
        return 0;
    }
    cases++;return 1;
}

static int sprite(int x,int y,int width,int height,int entry)
{
    begin(entry==19?0x0E41:0x0EE6);
    const int block[12]={SRC,2,3,0,x,y,width,height,20,27,10,18};
    int32_t record[14+64]={entry,PAGE};
    for(int k=0;k<12;k++){w(SS,BLOCK+2*k,block[k]);record[2+k]=block[k];}
    for(int sy=0;sy<height;sy++)for(int sx=0;sx<width;sx++) {
        int value=(sx+2*sy)%3?1+sx+8*sy:0;
        mem[16*SRC+320*(3+sy)+2+sx]=(uint8_t)value;
        record[14+sy*width+sx]=value;
    }
    drawlist_record(&d,'S',record,14+width*height);
    return finish("sprite",(entry<<24)|((x&255)<<16)|((y&255)<<8)|(width<<4)|height);
}

static int ticks(int side,int cl,int dl,int row,int low,int high)
{
    begin(0x071A);
    w(CS,0x071D,DS); /* the loader-relocated data-segment operand */
    const int table=side+(cl?4:0);
    w(DS,0x1966+table,100);w(DS,0x196E+table,low);w(DS,0x1976+table,high);
    mem[16*DS+0x1866]=0x73;
    cpu.r[R_SI]=(uint16_t)side;cpu.r[R_BX]=(uint16_t)row;
    cpu.r[R_CX]=(uint16_t)cl;cpu.r[R_DX]=(uint16_t)dl;
    const int32_t record[9]={side,row,dl,cl,0x73,100,low,high,PAGE};
    drawlist_record(&d,'H',record,9);
    return finish("tick",side+10*cl+100*dl+1000*row);
}

static int scaled_sprite(int x,int y,int width,int height)
{
    begin(0x0B70);
    const int block[11]={0,0,0,0,0,0,0,10,17,20,28};
    for(int k=0;k<11;k++)w(SS,BLOCK+2*k,block[k]);
    w(SS,SP+4,BLOCK);w(SS,SP+6,x);w(SS,SP+8,y);
    w(SS,SP+10,width);w(SS,SP+12,height);w(SS,SP+14,0);w(SS,SP+16,SRC);
    w(SRC,0,5);w(SRC,2,4);
    const uint8_t rows[]={3,0,1,0,7,0,9, 5,0,0,0,1,2,0,4,5,
                          0,0,5,0, 2,0,2,0,6,8};
    memcpy(mem+16*SRC+4,rows,sizeof rows);
    int32_t record[22+sizeof rows]={22,PAGE};
    for(int k=0;k<11;k++)record[2+k]=block[k];
    record[13]=x;record[14]=y;record[15]=width;record[16]=height;
    record[17]=SRC;record[18]=0;record[19]=5;record[20]=4;record[21]=sizeof rows;
    for(unsigned k=0;k<sizeof rows;k++)record[22+k]=rows[k];
    drawlist_record(&d,'W',record,sizeof record/sizeof *record);
    return finish("scaled sprite",1000000*(x+8)+10000*(y+8)+100*(width+12)+height+12);
}

static int text(int entry,int mode,int fixed,int x,int y,int left,int right,int top,int bottom)
{
    const int entries[]={0,0x0224,0x020E,0x0219,0x022F,0x019A,0x0203};
    begin(entries[entry]);w(CS,0x03EC,DS);
    const int block[11]={0,mode,0x71,0x29,x,y,0,top,bottom,left,right};
    for(int k=0;k<11;k++)w(SS,BLOCK+2*k,block[k]);
    const uint8_t string[]={'A',0x85,'B','Z','C',0};
    memcpy(mem+16*SS+STRING,string,sizeof string);
    cpu.r[R_BX]=STRING;
    if(entry==5){w(SS,SP+4,BLOCK);w(SS,SP+6,STRING);}
    w(DS,0xD0,0);w(DS,0xD2,FONT);
    const int header[7]={'A','C',2,fixed,3,1,1};
    for(int k=0;k<7;k++)mem[16*DS+FONT-8+k]=(uint8_t)header[k];
    const int widths[3]={3,4,5};
    for(int k=0;k<3;k++)mem[16*DS+FONT-11+k]=(uint8_t)widths[k];
    for(int r=0;r<4;r++)for(int c=0;c<3;c++) {
        mem[16*DS+FONT+6*r+2*c]=(uint8_t)((0xA8u>>(r&1))^(c*0x28));
        mem[16*DS+FONT+6*r+2*c+1]=0;
    }
    int32_t record[128]={entry,PAGE};int n=2;
    for(int k=0;k<11;k++)record[n++]=block[k];
    record[n++]=5;for(int k=0;k<5;k++)record[n++]=string[k];
    record[n++]=1;for(int k=0;k<7;k++)record[n++]=header[k];
    for(int k=0;k<3;k++)record[n++]=widths[k];
    record[n++]=26;for(int k=0;k<26;k++)record[n++]=mem[16*DS+FONT+k];
    drawlist_record(&d,'T',record,n);
    return finish("text",entry+10*mode+100*fixed+1000*x+100000*y);
}

int main(void)
{
    for(unsigned k=0;k<RC_NMODULES;k++)if(!strcmp(RC_MODULES[k]->name,"MGRAPHIC.EXE"))module=RC_MODULES[k];
    if(!module){fputs("MGRAPHIC translation required\n",stderr);return 2;}
    drawlist_init(&d);
    const int xs[]={-4,6,9,10,12,18,19,22},ys[]={15,19,20,23,27,28,31};
    for(unsigned x=0;x<sizeof xs/sizeof *xs;x++)for(unsigned y=0;y<sizeof ys/sizeof *ys;y++)
        for(int width=1;width<=7;width+=3)for(int height=1;height<=7;height+=3)
            if(!sprite(xs[x],ys[y],width,height,19))return 1;
    if(!sprite(10,20,7,7,18))return 1;
    const int pos[]={-8,-1,0,2,5,7,9},sizes[]={-11,-6,-3,-1,1,3,6,11};
    for(unsigned x=0;x<sizeof pos/sizeof *pos;x++)for(unsigned y=0;y<sizeof pos/sizeof *pos;y++)
        for(unsigned wi=0;wi<sizeof sizes/sizeof *sizes;wi++)for(unsigned hi=0;hi<sizeof sizes/sizeof *sizes;hi++)
            if(!scaled_sprite(pos[x],pos[y],sizes[wi],sizes[hi]))return 1;
    for(int side=0;side<=2;side+=2)for(int cl=0;cl<2;cl++)for(int dl=-1;dl<=1;dl++)
        for(int row=1;row<=40;row+=13)for(int high=20;high<=40;high+=20)
            if(!ticks(side,cl,dl,row,3,high))return 1;
    for(int entry=1;entry<=6;entry++)for(int mode=0;mode<=1;mode++)for(int fixed=0;fixed<=4;fixed+=4)
        for(int x=8;x<=22;x+=7)for(int y=8;y<=16;y+=4)
            if(!text(entry,mode,fixed,x,y,11,20,10,14))return 1;
    drawlist_free(&d);
    printf("presentation driver: %u staged cases equal to original bytes; no copied output\n",cases);
    return 0;
}
