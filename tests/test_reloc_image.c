/* Synthetic loader and original-versus-C checks; no game files needed. */
#include <stdio.h>
#include <stdlib.h>
#include "reloc_image.h"

static uint8_t memory[2][MEM_SIZE], saved[MEM_SIZE];
static int failures;
#define REQUIRE(ok) do { if (!(ok)) { printf("failed line %d: %s\n", __LINE__, #ok); failures++; } } while (0)
static const uint8_t load_code[] = { 0xB8,0,0, 0x8E,0xD8, 0xA1,0x20,0, 0xC3 };
static const uint8_t words[] = { 0xFF,0xFF,0xAB,0xCD,0xEF,0x12,0x7F,0 };
static const uint8_t com_code[] = { 0x90,0x40,0xC3 };
static const rc_module load = { .name="LOAD.EXE", .file_hash=0x11,
    .size=sizeof load_code, .image=load_code };
static const rc_module word = { .name="WORD.EXE", .file_hash=0x22,
    .size=sizeof words, .image=words };
static const rc_module com = { .name="PLAIN.COM", .file_hash=0x33,
    .size=sizeof com_code, .image=com_code, .origin=0x100 };

static void instruction_case(uint16_t base, int hardcoded)
{
    memset(memory, 0, sizeof memory);
    REQUIRE(test_load_generated_image(memory[0], MEM_SIZE, &load, base));
    const uint32_t data = (uint32_t)base * 16u + 0x20;
    memory[0][data] = 0x34; memory[0][data + 1] = 0x12;
    /* A hardcoded zero DS can read a different value as well as using the
     * wrong segment; base zero is the control where the error is invisible. */
    if (base) { memory[0][0x20] = 0xCD; memory[0][0x21] = 0xAB; }
    memcpy(memory[1], memory[0], MEM_SIZE);
    cpu_t a, b;
    cpu_init(&a, memory[0]); cpu_init(&b, memory[1]);
    a.model = b.model = CPU_80286;
    a.seg[S_CS] = b.seg[S_CS] = base;
    a.seg[S_SS] = b.seg[S_SS] = 0x6000;
    a.r[R_SP] = b.r[R_SP] = 0x8000;
    a.flags = b.flags = 0x0202;
    a.stop_at = b.stop_at = UINT64_MAX;
    cpu_push16(&a, 0xF000); cpu_push16(&b, 0xF000);
    for (int i = 0; i < 4; i++) cpu_step(&a);
    REQUIRE(a.ip == 0xF000 && a.icount == 4 && a.r[R_AX] == 0x1234);
    b.r[R_AX] = hardcoded ? 0 : seg_read16(&b, b.seg[S_CS], 1); b.icount++;
    b.seg[S_DS] = b.r[R_AX]; b.icount++;
    b.r[R_AX] = seg_read16(&b, b.seg[S_DS], 0x20); b.icount++;
    b.ip = cpu_pop16(&b); b.icount++;
    const int same = !memcmp(a.r,b.r,sizeof a.r) && !memcmp(a.seg,b.seg,sizeof a.seg) &&
        a.ip==b.ip && a.flags==b.flags && a.icount==b.icount &&
        a.halted==b.halted && a.inhibit_at==b.inhibit_at && a.int_depth==b.int_depth &&
        !memcmp(memory[0],memory[1],MEM_SIZE);
    REQUIRE(same == (!hardcoded || !base));
    printf("base %04X %s: %s\n", base, hardcoded ? "DS=0 mutant" : "loaded operand", same ? "equal" : "different");
}

static void rejection_cases(void)
{
    const char *names[] = { "LOAD.EXE", "LOAD.EXE" };
    const uint64_t hashes[] = { 0x11, 0x11 };
    uint32_t starts[] = { 0, 1, 2 }, offsets[] = { 1, 1 };
    test_reloc_table t = { names, hashes, starts, offsets, 1, 1 };
    memset(memory[0], 0xA5, MEM_SIZE); memcpy(saved, memory[0], MEM_SIZE);
    const uint32_t bad[] = { sizeof load_code - 1, sizeof load_code, UINT32_MAX };
    for (unsigned k=0;k<sizeof bad/sizeof bad[0];k++) {
        offsets[0]=bad[k]; REQUIRE(!test_load_image(memory[0],MEM_SIZE,&load,0x1000,&t));
        REQUIRE(!memcmp(saved,memory[0],MEM_SIZE));
    }
    offsets[0]=1; t.modules=2; t.count=2;
    REQUIRE(!test_load_image(memory[0],MEM_SIZE,&load,0x1000,&t)); /* duplicate identity */
    t.modules=1; starts[1]=2; offsets[1]=2;
    REQUIRE(!test_load_image(memory[0],MEM_SIZE,&load,0x1000,&t)); /* overlapping words */
    starts[1]=0;
    REQUIRE(!test_load_image(memory[0],MEM_SIZE,&load,0x1000,&t)); /* wrong final count */
    starts[0]=1; starts[1]=2;
    REQUIRE(!test_load_image(memory[0],MEM_SIZE,&load,0x1000,&t)); /* missing initial zero */
    rc_module missing=load; missing.file_hash=0x99;
    REQUIRE(!test_load_generated_image(memory[0],MEM_SIZE,&missing,0x1000));
    REQUIRE(!test_load_generated_image(memory[0],MEM_SIZE,&com,0xFFFF));
    REQUIRE(!test_load_generated_image(memory[0],8,&load,0));
    REQUIRE(!memcmp(saved,memory[0],MEM_SIZE));
}

int main(void)
{
    memset(memory[0],0xA5,MEM_SIZE);
    REQUIRE(test_load_generated_image(memory[0],MEM_SIZE,&word,2));
    REQUIRE(memory[0][32]==1 && memory[0][33]==0 && memory[0][38]==0x81 && memory[0][39]==0);
    REQUIRE(!memcmp(memory[0]+34,words+2,4) && memory[0][31]==0xA5 && memory[0][40]==0xA5);
    memset(memory[0],0xA5,MEM_SIZE);
    REQUIRE(test_load_generated_image(memory[0],MEM_SIZE,&com,0x1234));
    const uint32_t at=0x12340+0x100;
    REQUIRE(!memcmp(memory[0]+at,com_code,sizeof com_code) && memory[0][at-1]==0xA5 && memory[0][at+3]==0xA5);
    rejection_cases();
    for (unsigned i=0;i<3;i++) {
        const uint16_t base=i==0 ? 0 : i==1 ? 0x1000 : 0x3000;
        instruction_case(base,0); instruction_case(base,1);
    }
    printf("loader checks: %d failed\n",failures);
    return failures != 0;
}
