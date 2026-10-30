/* controls.h - controller settings for the game port (docs/roadmap.md, Phase 3 "Modern controls").
 *
 * The game reads a two-axis, two-button joystick. This maps a controller onto it: which axes drive
 * the stick, a dead zone, optional inversion and which controller buttons are fire 1 and fire 2.
 * Values come from f117a.ini keys (pad-deadzone, pad-invert-x, pad-invert-y, pad-fire1, pad-fire2),
 * so the settings menu and the file agree. Pure logic: SDL stays in main.c. */
#ifndef F117R_CONTROLS_H
#define F117R_CONTROLS_H

typedef struct {
    int deadzone;          /* percent of full travel ignored around centre, 0..50 */
    int invert_x, invert_y;
    int fire1, fire2;      /* controller button numbers (SDL gamepad order: 0 south, 1 east, 2 west, 3 north...) */
} controls_config;

/* the original mapping: no dead zone, no inversion, south and east */
void controls_defaults(controls_config *c);

/* Fill c from the INI text (keys above); anything missing or out of range keeps its default. Returns
 * the number of keys read. */
int controls_load(controls_config *c, const char *ini_text);

/* A raw axis reading (-32768..32767) as the game port's 0..255, centre 128-ish, after the dead zone
 * (rescaled so full travel still reaches the ends) and inversion. */
unsigned controls_axis(const controls_config *c, int raw, int invert);

/* The game port's button bits (bit 0 fire 1, bit 1 fire 2) from a bitmask of the controller's buttons
 * (bit n = button n held). */
unsigned controls_buttons(const controls_config *c, unsigned held_mask);

#endif
