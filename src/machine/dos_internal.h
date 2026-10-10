/* Private DOS/BIOS service interface; not a host API. */
#ifndef F117R_DOS_INTERNAL_H
#define F117R_DOS_INTERNAL_H

#include "machine.h"
#include <time.h>

#define ERR_BAD_FUNCTION   0x01
#define ERR_FILE_NOT_FOUND 0x02
#define ERR_PATH_NOT_FOUND 0x03
#define ERR_TOO_MANY_OPEN  0x04
#define ERR_ACCESS_DENIED  0x05
#define ERR_BAD_HANDLE     0x06
#define ERR_NO_MEMORY      0x08
#define ERR_BAD_FORMAT     0x0B
#define ERR_NO_MORE_FILES  0x12

/* BIOS data area, as linear addresses. */
#define BDA_EQUIPMENT   0x410
#define BDA_MEM_KB      0x413
#define BDA_SHIFT       0x417
#define BDA_SHIFT2      0x418
#define BDA_KBD_HEAD    0x41A
#define BDA_KBD_TAIL    0x41C
#define BDA_KBD_BUF     0x41E   /* 16 words */
#define BDA_KBD_BUF_END 0x43E
#define BDA_VIDEO_MODE  0x449
#define BDA_VIDEO_COLS  0x44A
#define BDA_PAGE_SIZE   0x44C
#define BDA_CURSOR_POS  0x450
#define BDA_CURSOR_TYPE 0x460
#define BDA_CRTC_BASE   0x463
#define BDA_TICKS       0x46C
#define BDA_MIDNIGHT    0x470
#define BDA_KBD_START   0x480
#define BDA_KBD_END     0x482
#define BDA_ROWS_M1     0x484
#define BDA_CHAR_HEIGHT 0x485
#define BDA_KBD_FLAGS3  0x496


typedef struct {
    uint16_t env_seg;
    uint16_t cmd_seg, cmd_off;
    uint16_t fcb1_seg, fcb1_off;
    uint16_t fcb2_seg, fcb2_off;
    uint16_t parent_psp;
    uint16_t parent_env;     /* used when env_seg is 0 */
    uint16_t flags;          /* the caller's flags at the INT 21h */
} exec_params;

void dos_console_text(machine_t *m, const char *s, size_t n);
int dos_int10(machine_t *m);
void dos_sft_release(machine_t *m, uint8_t sft);
void dos_close_files_of(machine_t *m, uint16_t owner);
void dos_wall_clock(machine_t *m, struct tm *out, unsigned *centis);
uint8_t dos_bcd(unsigned v);
int dos_int21(machine_t *m);
void dos_mcb_set_type(machine_t *m, uint16_t s, uint8_t t);
void dos_mcb_set_owner(machine_t *m, uint16_t s, uint16_t o);
void dos_mcb_set_size(machine_t *m, uint16_t s, uint16_t n);
void dos_mcb_set_name(machine_t *m, uint16_t s, const uint8_t name[8]);
int dos_mem_alloc(machine_t *m, uint16_t *seg_out, uint16_t *paras);
uint16_t dos_mem_resize(machine_t *m, uint16_t seg, uint16_t *paras);
uint16_t dos_mem_free(machine_t *m, uint16_t seg);
void dos_mem_free_process(machine_t *m, uint16_t psp);
int dos_kbd_pop(machine_t *m, uint16_t *key, int remove);
void dos_bios_key_irq(machine_t *m);
int dos_int16(machine_t *m);
uint16_t dos_load_program(machine_t *m, const char *name, const exec_params *ep,
                             uint16_t ret_cs, uint16_t ret_ip, uint16_t *psp_out);
uint16_t dos_load_overlay(machine_t *m, const char *name, uint16_t load_seg,
                             uint16_t reloc_factor);
int dos_terminate(machine_t *m, uint8_t code);
void dos_ok(cpu_t *c);
void dos_fail(cpu_t *c, uint16_t err);
uint16_t dos_current_psp(const machine_t *m);
void dos_guest_str(cpu_t *c, uint16_t seg, uint16_t off, char *out, size_t n);
void dos_guest_write(cpu_t *c, uint32_t lin, const uint8_t *p, size_t n);
int dos_bios_wait(cpu_t *c);

const char *dos_basename(const char *p);
FILE *dos_open_read(machine_t *m, const char *name, char *found, size_t fn);
uint8_t *dos_read_whole(machine_t *m, const char *name, long *out_size);
void t386_file(machine_t *m, uint32_t cycles);

#endif
