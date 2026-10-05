/* inputlog.h - the input log: every key, mouse and stick event with the
 * clock count it reached the machine at.
 *
 *   # f117r-input ips=9000000 time_us=1759536000000000
 *   # f117r-fixes D5                 (only when fixes were on)
 *   K <icount> <byte hex>
 *   M <icount> <x> <y> <buttons> <dx> <dy>
 *   J <icount> <present> <axis0> <axis1> <axis2> <axis3> <buttons>
 *
 * With the same install, speed and boot time, replaying a log reproduces the
 * session to the instruction, under either engine. Written by the game
 * (--record) and the headless runner; read by both (--replay). */
#ifndef F117R_INPUTLOG_H
#define F117R_INPUTLOG_H

#include "machine.h"

#include <stdio.h>

void inputlog_header(FILE *f, uint64_t ips, uint64_t time_us);
void inputlog_write(FILE *f, const machine_input *in);
/* Read the header's speed and boot time, if the file has them. */
void inputlog_read_header(const char *path, uint64_t *ips, uint64_t *time_us);
/* The fixes a session ran with, as a second comment line
 * ("# f117r-fixes D5"); with none nothing is written, so a log of the
 * original is unchanged. Reading gives them space-separated, or "". */
void inputlog_fixes(FILE *f, const char *ids);
void inputlog_read_fixes(const char *path, char *ids, size_t n);

/* A replay, streamed: each input is queued on the machine before its time
 * comes (call inputlog_feed with a horizon past the next slice's end). */
typedef struct inputlog_reader inputlog_reader;
inputlog_reader *inputlog_open(const char *path);
void inputlog_feed(inputlog_reader *r, machine_t *m, uint64_t horizon);
int  inputlog_done(const inputlog_reader *r);
void inputlog_close(inputlog_reader *r);

#endif
