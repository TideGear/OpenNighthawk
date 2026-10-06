/* test_config.c - f117a.ini: keys become the options they name, ahead of the
 * command line's. */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int same(char **v, int n, const char *const *want)
{
    for (int i = 0; i < n; i++)
        if (!want[i] || strcmp(v[i], want[i])) { printf("  arg %d: %s\n", i, v[i]); return 0; }
    return want[n] == NULL;
}

int main(void)
{
    char err[200], **v;
    char *argv[] = { "f117a", "--scale", "4", NULL };
    const char *text =
        "; my settings\r\n"
        "[game]\r\n"
        "data = D:\\GOG\\F-117A\r\n"
        "mt32-control = roms\\mt32_ctrl_1_07.rom\r\n"
        "mt32-pcm = \"C:\\ROMs\\mt32_pcm.rom\"\r\n"
        "  Fullscreen = Yes  \r\n"
        "no-aspect = no\r\n"
        "fix = D4, D5\r\n"
        "# the end\r\n";
    int n = config_merge(text, "C:\\Games\\f117\\", 3, argv, &v, err, sizeof err);
    const char *const want[] = { "f117a", "--data", "D:\\GOG\\F-117A",
        "--mt32-control", "C:\\Games\\f117\\roms\\mt32_ctrl_1_07.rom", "--mt32-pcm", "C:\\ROMs\\mt32_pcm.rom",
        "--fullscreen", "--fix", "D4", "--fix", "D5", CONFIG_CLI_MARK, "--scale", "4", NULL };
    CHECK(n == 15);
    CHECK(same(v, n, want));
    /* no file folder: a relative path stays as written */
    n = config_merge("save = saves\n", NULL, 1, argv, &v, err, sizeof err);
    const char *const want2[] = { "f117a", "--save", "saves", CONFIG_CLI_MARK, NULL };
    CHECK(n == 4 && same(v, n, want2));
    /* errors name the line */
    CHECK(config_merge("data = x\nvolume = 11\n", NULL, 1, argv, &v, err, sizeof err) < 0);
    CHECK(strstr(err, "line 2") && strstr(err, "volume"));
    CHECK(config_merge("fullscreen = maybe\n", NULL, 1, argv, &v, err, sizeof err) < 0);
    CHECK(config_merge("data\n", NULL, 1, argv, &v, err, sizeof err) < 0);
    CHECK(config_merge("data =\n", NULL, 1, argv, &v, err, sizeof err) < 0);
    /* --config names the file; --no-config skips the default */
    int explicit_;
    char *a1[] = { "f117a", "--config", "x.ini", NULL };
    CHECK(!strcmp(config_path(3, a1, "C:\\nowhere\\", &explicit_), "x.ini") && explicit_);
    char *a2[] = { "f117a", "--no-config", NULL };
    CHECK(config_path(2, a2, "C:\\nowhere\\", &explicit_) == NULL && !explicit_);
    puts("f117a.ini options pass");
    return 0;
}
