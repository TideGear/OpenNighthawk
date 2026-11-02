/* test_controls.c - controller mapping: defaults match the original port, dead zone, inversion, buttons. */
#include "controls.h"

#include <stdio.h>
#include <string.h>

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
    /* buttons that act as keys */
    {
        int b[4]; char nm[4][CONTROLS_KEYNAME];
        CHECK(controls_pad_keys("", b, nm, 4) == 0);
        CHECK(controls_pad_keys("2:G, 3:Left Shift", b, nm, 4) == 2 && b[0] == 2 && !strcmp(nm[0], "G") && b[1] == 3 && !strcmp(nm[1], "Left Shift"));
        CHECK(controls_pad_keys("0:Space", b, nm, 4) == 1 && !strcmp(nm[0], "Space"));
        CHECK(controls_pad_keys("x:G", b, nm, 4) == -1 && controls_pad_keys("40:G", b, nm, 4) == -1);
        CHECK(controls_pad_keys("2:", b, nm, 4) == -1 && controls_pad_keys("2G", b, nm, 4) == -1);
        CHECK(controls_pad_keys("1:A,2:B,3:C", b, nm, 2) == -1);          /* more than room */
    }
    puts("controller mapping passes");
    return 0;
}
