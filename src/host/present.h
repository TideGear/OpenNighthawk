/* present.h - turning the emulated VGA's state into pixels.
 *
 * Mode-13h frames come from the machine's four-part scanout, matching the
 * GOG DOSBox renderer. Text mode and the interval before the first completed
 * graphics frame use a snapshot. Drawing during scanout can cause tearing.
 */
#ifndef F117R_PRESENT_H
#define F117R_PRESENT_H

#include "machine.h"

typedef struct {
    int      text;                  /* 80x25 text page, else mode 13h */
    int      blank;                 /* the sequencer's screen-off bit */
    uint8_t  vram[64000];           /* mode 13h pixels, or the text page */
    uint8_t  dac[768];
    uint8_t  pel_mask;
    uint16_t cursor_pos, cursor_type;
    uint64_t icount;
} present_frame;

/* Copy the last completed graphics frame, or the current text page. */
void present_capture(const machine_t *m, present_frame *f);

/* Render a frame to 32-bit 0xAARRGGBB pixels. Text pages are 640x400,
 * mode 13h is 320x200; *w and *h say which. `out` holds 640*400 pixels.
 * `blink` is the text cursor/attribute blink phase (0 or 1). */
void present_render(const present_frame *f, uint32_t *out, int *w, int *h, int blink);

/* Write the machine's current screen as a binary PPM. */
int present_write_ppm(const machine_t *m, const char *path);

/* Write a captured frame as a binary PPM. */
int present_frame_write_ppm(const present_frame *f, const char *path);

#endif
