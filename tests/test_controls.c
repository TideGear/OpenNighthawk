/* test_controls.c - controller mapping: defaults match the original port, dead zone, inversion, buttons. */
#include "controls.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    controls_config c;
    CHECK(controls_load(&c, "") == 0);
    /* the defaults give the mapping main.c used before: (raw/32767 + 1) * 127.5 */
    CHECK(controls_axis(&c, -32767, 0) == 0 && controls_axis(&c, 32767, 0) == 255);
    CHECK(controls_axis(&c, 0, 0) == 127);
    CHECK(controls_buttons(&c, 1) == 1 && controls_buttons(&c, 2) == 2 && controls_buttons(&c, 3) == 3);
    /* a dead zone swallows small movements and still reaches the ends */
    CHECK(controls_load(&c, "pad-deadzone = 20\n") == 1);
    CHECK(controls_axis(&c, 5000, 0) == 127 && controls_axis(&c, -6000, 0) == 127);
    CHECK(controls_axis(&c, 32767, 0) == 255 && controls_axis(&c, -32767, 0) == 0);
    CHECK(controls_axis(&c, 20000, 0) > 127 && controls_axis(&c, 20000, 0) < 255);
    /* inversion mirrors about the centre; buttons can be remapped */
    CHECK(controls_load(&c, "pad-invert-y = yes\npad-fire1 = 2\npad-fire2 = 3\n") == 3);
    CHECK(c.invert_y && !c.invert_x);
    CHECK(controls_axis(&c, 32767, c.invert_y) == 0 && controls_axis(&c, -32767, c.invert_y) == 255);
    CHECK(controls_buttons(&c, 1) == 0 && controls_buttons(&c, 4) == 1 && controls_buttons(&c, 8) == 2);
    /* out-of-range or bad values keep the defaults */
    CHECK(controls_load(&c, "pad-deadzone = 90\npad-fire1 = x\npad-fire2 = 40\n") == 0);
    CHECK(c.deadzone == 0 && c.fire1 == 0 && c.fire2 == 1);
    puts("controller mapping passes");
    return 0;
}
