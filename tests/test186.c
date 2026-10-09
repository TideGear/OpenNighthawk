/* test186.c - the 80186/80286 additions to the oracle, and the 8086
 * behaviours they replace.
 *
 * The SingleStepTests vectors cover the 8086 exhaustively but say nothing
 * about the 186/286 instructions the game's flight engine is compiled with
 * (ENTER/LEAVE, PUSH imm, IMUL imm, shifts by immediate, PUSHA/POPA), nor
 * about the corner behaviours that change between the parts. This checks
 * each of those by hand against the documented semantics, and checks that
 * the same bytes still do the 8086 thing when the model is 8086.
 */
#include "cpu.h"
#include "timing386.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);       \
            printf(__VA_ARGS__);                                \
            printf("\n");                                       \
            failures++;                                         \
        }                                                       \
    } while (0)

static uint8_t *mem;
static cpu_t cpu;

#define CODE_SEG 0x1000
#define STK_SEG  0x2000

static void setup(int model)
{
    memset(mem, 0, MEM_SIZE);
    cpu_init(&cpu, mem);
    cpu.model = model;
    cpu.seg[S_CS] = CODE_SEG; cpu.ip = 0;
    cpu.seg[S_SS] = STK_SEG;  cpu.r[R_SP] = 0x1000;
    cpu.seg[S_DS] = cpu.seg[S_ES] = 0x3000;
    cpu.flags = cpu_flags_fixed(&cpu);
}

static void code(const uint8_t *b, size_t n) { memcpy(mem + CODE_SEG * 16, b, n); }
#define CODE(...) do { static const uint8_t b_[] = { __VA_ARGS__ }; code(b_, sizeof b_); } while (0)

static uint16_t stk(uint16_t off) { return seg_read16(&cpu, STK_SEG, off); }
static void stk_set(uint16_t off, uint16_t v) { seg_write16(&cpu, STK_SEG, off, v); }
static int step(void) { return cpu_step(&cpu); }

/* ---- ENTER / LEAVE ------------------------------------------------------ */
static void test_enter_leave(void)
{
    printf("ENTER/LEAVE (286 mode)\n");
    setup(CPU_80286);
    cpu.r[R_BP] = 0x1234;
    CODE(0xC8, 0x08, 0x00, 0x00,    /* enter 8, 0 */
         0xC9);                     /* leave */
    step();
    CHECK(cpu.r[R_BP] == 0x0FFE, "enter: BP=%04X, want 0FFE", cpu.r[R_BP]);
    CHECK(cpu.r[R_SP] == 0x0FF6, "enter: SP=%04X, want 0FF6", cpu.r[R_SP]);
    CHECK(stk(0x0FFE) == 0x1234, "enter: saved BP=%04X, want 1234", stk(0x0FFE));
    step();
    CHECK(cpu.r[R_SP] == 0x1000 && cpu.r[R_BP] == 0x1234,
          "leave: SP=%04X BP=%04X, want 1000/1234", cpu.r[R_SP], cpu.r[R_BP]);

    printf("ENTER with nesting level 2\n");
    setup(CPU_80286);
    cpu.r[R_BP] = 0x0F00;
    stk_set(0x0EFE, 0xAAAA);        /* the enclosing frame's saved pointer */
    CODE(0xC8, 0x04, 0x00, 0x02);   /* enter 4, 2 */
    step();
    CHECK(cpu.r[R_BP] == 0x0FFE, "nested: BP=%04X, want 0FFE", cpu.r[R_BP]);
    CHECK(cpu.r[R_SP] == 0x0FF6, "nested: SP=%04X, want 0FF6", cpu.r[R_SP]);
    CHECK(stk(0x0FFE) == 0x0F00, "nested: [FFE]=%04X, want 0F00", stk(0x0FFE));
    CHECK(stk(0x0FFC) == 0xAAAA, "nested: [FFC]=%04X, want AAAA", stk(0x0FFC));
    CHECK(stk(0x0FFA) == 0x0FFE, "nested: [FFA]=%04X, want 0FFE", stk(0x0FFA));

    printf("C8/C9 are RETF aliases on the 8086\n");
    setup(CPU_8086);
    stk_set(0x1000, 0x0005);        /* IP */
    stk_set(0x1002, 0x4000);        /* CS */
    CODE(0xC8, 0x02, 0x00);         /* 8086: retf 2 */
    step();
    CHECK(cpu.seg[S_CS] == 0x4000 && cpu.ip == 0x0005,
          "8086 C8: CS:IP=%04X:%04X, want 4000:0005", cpu.seg[S_CS], cpu.ip);
    CHECK(cpu.r[R_SP] == 0x1006, "8086 C8: SP=%04X, want 1006", cpu.r[R_SP]);
}

/* ---- PUSHA / POPA -------------------------------------------------------- */
static void test_pusha_popa(void)
{
    printf("PUSHA/POPA\n");
    setup(CPU_80186);
    cpu.r[R_AX] = 1; cpu.r[R_CX] = 2; cpu.r[R_DX] = 3; cpu.r[R_BX] = 4;
    cpu.r[R_BP] = 5; cpu.r[R_SI] = 6; cpu.r[R_DI] = 7;
    CODE(0x60,                       /* pusha */
         0xB8, 0x00, 0x00,           /* mov ax, 0  (clobber) */
         0x61);                      /* popa */
    step();
    CHECK(cpu.r[R_SP] == 0x0FF0, "pusha: SP=%04X, want 0FF0", cpu.r[R_SP]);
    CHECK(stk(0x0FFE) == 1 && stk(0x0FFC) == 2 && stk(0x0FFA) == 3 && stk(0x0FF8) == 4,
          "pusha: AX CX DX BX order wrong");
    CHECK(stk(0x0FF6) == 0x1000, "pusha: pushed SP=%04X, want the original 1000", stk(0x0FF6));
    CHECK(stk(0x0FF4) == 5 && stk(0x0FF2) == 6 && stk(0x0FF0) == 7,
          "pusha: BP SI DI order wrong");
    step(); step();
    CHECK(cpu.r[R_AX] == 1 && cpu.r[R_CX] == 2 && cpu.r[R_DX] == 3 && cpu.r[R_BX] == 4 &&
          cpu.r[R_BP] == 5 && cpu.r[R_SI] == 6 && cpu.r[R_DI] == 7,
          "popa did not restore the registers");
    CHECK(cpu.r[R_SP] == 0x1000, "popa: SP=%04X, want 1000", cpu.r[R_SP]);

    printf("0x60-0x6F are Jcc aliases on the 8086\n");
    setup(CPU_8086);
    cpu.flags |= F_ZF;
    CODE(0x64, 0x10);                /* 8086: alias of JZ +10h */
    step();
    CHECK(cpu.ip == 0x0012, "8086 0x64 with ZF: IP=%04X, want 0012", cpu.ip);
}

/* ---- PUSH imm ------------------------------------------------------------ */
static void test_push_imm(void)
{
    printf("PUSH imm8 (sign-extended) and imm16\n");
    setup(CPU_80186);
    CODE(0x6A, 0xF0,                 /* push -16 */
         0x68, 0x34, 0x12);          /* push 1234h */
    step();
    CHECK(stk(0x0FFE) == 0xFFF0, "push imm8: got %04X, want FFF0", stk(0x0FFE));
    step();
    CHECK(stk(0x0FFC) == 0x1234, "push imm16: got %04X, want 1234", stk(0x0FFC));
    CHECK(cpu.r[R_SP] == 0x0FFC, "SP=%04X, want 0FFC", cpu.r[R_SP]);
}

/* ---- IMUL r16, r/m16, imm ------------------------------------------------ */
static void test_imul_imm(void)
{
    printf("IMUL three-operand\n");
    setup(CPU_80186);
    cpu.r[R_AX] = 3;
    CODE(0x6B, 0xC0, 0xFB);          /* imul ax, ax, -5 */
    step();
    CHECK(cpu.r[R_AX] == 0xFFF1, "3 * -5: AX=%04X, want FFF1", cpu.r[R_AX]);
    CHECK(!(cpu.flags & F_CF) && !(cpu.flags & F_OF), "3 * -5 must not set CF/OF");

    setup(CPU_80186);
    cpu.r[R_AX] = 4;
    CODE(0x69, 0xC0, 0x00, 0x40);    /* imul ax, ax, 4000h */
    step();
    CHECK(cpu.r[R_AX] == 0x0000, "4 * 4000h: AX=%04X, want 0000", cpu.r[R_AX]);
    CHECK((cpu.flags & F_CF) && (cpu.flags & F_OF), "4 * 4000h overflows: CF/OF must be set");
}

/* ---- shifts -------------------------------------------------------------- */
static void test_shifts(void)
{
    printf("shift by immediate (C1)\n");
    setup(CPU_80186);
    cpu.r[R_AX] = 0x1234;
    CODE(0xC1, 0xE0, 0x04);          /* shl ax, 4 */
    step();
    CHECK(cpu.r[R_AX] == 0x2340, "shl ax,4: AX=%04X, want 2340", cpu.r[R_AX]);
    CHECK(cpu.flags & F_CF, "shl ax,4 of 1234h: CF must be 1 (bit 12 shifted out)");

    printf("C0/C1 are RET aliases on the 8086\n");
    setup(CPU_8086);
    stk_set(0x1000, 0x0042);
    CODE(0xC1);                      /* 8086: ret */
    step();
    CHECK(cpu.ip == 0x0042 && cpu.r[R_SP] == 0x1002,
          "8086 C1: IP=%04X SP=%04X, want 0042/1002", cpu.ip, cpu.r[R_SP]);

    printf("shift count masking\n");
    setup(CPU_80186);
    cpu.r[R_AX] = 1; cpu.r[R_CX] = 0x21;
    CODE(0xD3, 0xE0);                /* shl ax, cl  (cl = 33) */
    step();
    CHECK(cpu.r[R_AX] == 2, "186 masks 33 to 1: AX=%04X, want 0002", cpu.r[R_AX]);
    setup(CPU_8086);
    cpu.r[R_AX] = 1; cpu.r[R_CX] = 0x21;
    CODE(0xD3, 0xE0);
    step();
    CHECK(cpu.r[R_AX] == 0, "8086 shifts all 33: AX=%04X, want 0000", cpu.r[R_AX]);

    printf("group-2 /6: SETMO on the 8086, SHL on the 186\n");
    setup(CPU_8086);
    cpu.r[R_AX] = 0x0001;
    CODE(0xD0, 0xF0);                /* /6 on AL */
    step();
    CHECK((cpu.r[R_AX] & 0xFF) == 0xFF, "8086 /6: AL=%02X, want FF", cpu.r[R_AX] & 0xFF);
    setup(CPU_80186);
    cpu.r[R_AX] = 0x0001;
    CODE(0xD0, 0xF0);
    step();
    CHECK((cpu.r[R_AX] & 0xFF) == 0x02, "186 /6: AL=%02X, want 02", cpu.r[R_AX] & 0xFF);
}

/* ---- PUSH SP, 0F, FLAGS bits --------------------------------------------- */
static void test_model_corners(void)
{
    printf("PUSH SP\n");
    setup(CPU_8086);
    CODE(0x54);
    step();
    CHECK(stk(0x0FFE) == 0x0FFE, "8086 push sp stores the new SP: got %04X", stk(0x0FFE));
    setup(CPU_80286);
    CODE(0x54);
    step();
    CHECK(stk(0x0FFE) == 0x1000, "286 push sp stores the old SP: got %04X", stk(0x0FFE));

    printf("opcode 0F\n");
    setup(CPU_8086);
    stk_set(0x1000, 0x5000);
    CODE(0x0F);                      /* 8086: pop cs */
    step();
    CHECK(cpu.seg[S_CS] == 0x5000, "8086 0F is POP CS: CS=%04X", cpu.seg[S_CS]);
    setup(CPU_80286);
    CODE(0x0F, 0x00);
    int rc = step();
    CHECK(rc == STOP_FAULT, "286 0F must fault in this real-mode model, got %d", rc);

    printf("FLAGS bits 12-15\n");
    setup(CPU_8086);
    cpu.flags = 0x0002;
    CODE(0x9C);                      /* pushf */
    step();
    CHECK((stk(0x0FFE) & 0xF000) == 0xF000, "8086 pushf: top bits read as 1, got %04X", stk(0x0FFE));
    setup(CPU_80286);
    cpu.flags = 0x0002;
    CODE(0x9C);
    step();
    CHECK((stk(0x0FFE) & 0xF000) == 0x0000, "286 pushf: top bits read as 0, got %04X", stk(0x0FFE));

    printf("divide-error return address\n");
    setup(CPU_80286);
    mem_write16(&cpu, 0, 0x0100); mem_write16(&cpu, 2, 0x7000);  /* INT 0 -> 7000:0100 */
    cpu.r[R_AX] = 5; cpu.r[R_BX] = 0;
    CODE(0x90, 0xF7, 0xF3);          /* nop ; div bx */
    step(); step();
    CHECK(cpu.seg[S_CS] == 0x7000 && cpu.ip == 0x0100, "286 div by zero must take INT 0");
    CHECK(stk(0x0FFA) == 0x0001, "286 pushes the FAULTING IP (0001), got %04X", stk(0x0FFA));
    setup(CPU_8086);
    mem_write16(&cpu, 0, 0x0100); mem_write16(&cpu, 2, 0x7000);
    cpu.r[R_AX] = 5; cpu.r[R_BX] = 0;
    CODE(0x90, 0xF7, 0xF3);
    step(); step();
    CHECK(stk(0x0FFA) == 0x0003, "8086 pushes the NEXT IP (0003), got %04X", stk(0x0FFA));
}

/* ---- the misaligned-decode scenario that exposed all this ---------------- */
static void test_real_prologue(void)
{
    printf("a real MSC /G2 prologue runs straight in 286 mode\n");
    setup(CPU_80286);
    cpu.r[R_BP] = 0x0ABC; cpu.r[R_DI] = 0x1111; cpu.r[R_SI] = 0x2222;
    /* enter 0Ah,0 ; push di ; push si ; ... ; pop si ; pop di ; leave ; retf */
    stk_set(0x1000, 0x0777); stk_set(0x1002, 0x0888);     /* far return address */
    CODE(0xC8, 0x0A, 0x00, 0x00, 0x57, 0x56, 0x5E, 0x5F, 0xC9, 0xCB);
    for (int i = 0; i < 7; i++) step();
    CHECK(cpu.seg[S_CS] == 0x0888 && cpu.ip == 0x0777,
          "did not return to the far caller: %04X:%04X", cpu.seg[S_CS], cpu.ip);
    CHECK(cpu.r[R_BP] == 0x0ABC && cpu.r[R_SP] == 0x1004,
          "frame not restored: BP=%04X SP=%04X", cpu.r[R_BP], cpu.r[R_SP]);
}

/* The shadow is an instruction boundary even when that instruction costs
 * several cycles, or the following zero-count shift costs no cycles. */
static void test_386_shadow(void)
{
    static const uint8_t cases[][6] = {
        { 0xFB, 0xC1, 0xE0, 0x00, 0x90, 0x90 },       /* STI */
        { 0x17, 0xC1, 0xE0, 0x00, 0x90, 0x90 },       /* POP SS */
        { 0x8E, 0xD0, 0xC1, 0xE0, 0x00, 0x90 },       /* MOV SS,AX */
        { 0x26, 0x8E, 0xD0, 0xC1, 0xE0, 0x00 },       /* prefixed MOV SS,AX */
    };
    printf("386 interrupt shadows follow instructions, not one-cycle clocks\n");
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        setup(CPU_80286);
        cpu.r[R_AX] = STK_SEG;
        stk_set(0x1000, STK_SEG);
        code(cases[i], sizeof cases[i]);
        t386_enable(&cpu, T386_MEM_CACHED, 0xA0000, 0x20000, 32);
        cpu.icount = 1000;
        step();
        CHECK(cpu.inhibit_at == cpu.icount, "case %u: shadow is not at retirement", i);
        const uint64_t before = cpu.icount;
        step();
        CHECK(cpu.icount == before, "case %u: shift by zero advanced the clock", i);
        CHECK(cpu.inhibit_at != cpu.icount, "case %u: shadow survived the next instruction", i);
    }
}

int main(void)
{
    mem = (uint8_t *)calloc(MEM_SIZE, 1);
    if (!mem) return 2;
    printf("== 80186/80286 mode ==\n");
    test_enter_leave();
    test_pusha_popa();
    test_push_imm();
    test_imul_imm();
    test_shifts();
    test_model_corners();
    test_real_prologue();
    test_386_shadow();
    printf(failures ? "\n%d FAILURE(S)\n" : "\nall passed\n", failures);
    free(mem);
    return failures ? 1 : 0;
}
