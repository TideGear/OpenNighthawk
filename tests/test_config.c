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
    /* the simple Roland keys: a choice and a folder, taken from the file's folder */
    n = config_merge("roland = munt\nmt32-roms = roms\n", "C:\\Games\\", 1, argv, &v, err, sizeof err);
    const char *const want3[] = { "f117a", "--roland", "munt", "--mt32-roms", "C:\\Games\\roms", CONFIG_CLI_MARK, NULL };
    CHECK(n == 6 && same(v, n, want3));
    n = config_merge("present-line-width = 2\n", NULL, 1, argv, &v, err, sizeof err);
    const char *const want_width[] = { "f117a", "--present-line-width", "2", CONFIG_CLI_MARK, NULL };
    CHECK(n == 4 && same(v, n, want_width));
    /* the re-release's two settings */
    n = config_merge("presentation = enhanced\nfixes = original\n", NULL, 1, argv, &v, err, sizeof err);
    const char *const want_settings[] = { "f117a", "--presentation", "enhanced", "--fixes", "original", CONFIG_CLI_MARK, NULL };
    CHECK(n == 6 && same(v, n, want_settings));
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
    /* writing a setting back keeps everything else */
    {
        char val[64];
        char *t;
        /* an active line is rewritten in place, duplicates dropped, CRLF kept */
        t = config_set("; mine\r\nscale = 2\r\nfullscreen = no\r\nscale = 5\r\n", "scale", "4");
        CHECK(t && !strcmp(t, "; mine\r\nscale = 4\r\nfullscreen = no\r\n"));
        free(t);
        /* a commented example becomes the line, in its place */
        t = config_set("; The window.\n;scale = 3\n;fullscreen = no\n", "scale", "4");
        CHECK(t && !strcmp(t, "; The window.\nscale = 4\n;fullscreen = no\n"));
        free(t);
        /* prose that mentions a key is not mistaken for the example */
        t = config_set("; use scale = 2 for small windows\n", "scale", "3");
        CHECK(t && !strcmp(t, "; use scale = 2 for small windows\nscale = 3\n"));
        free(t);
        /* absent: appended, after a final line that lacks its break */
        t = config_set("data = x", "fixes", "original");
        CHECK(t && !strcmp(t, "data = x\nfixes = original\n"));
        free(t);
        t = config_set(NULL, "presentation", "enhanced");
        CHECK(t && !strcmp(t, "presentation = enhanced\n"));
        free(t);
        /* removal drops the active lines only */
        t = config_set("scale = 2\n;scale = 3\nfix = D4\n", "scale", NULL);
        CHECK(t && !strcmp(t, ";scale = 3\nfix = D4\n"));
        free(t);
        /* what config_set writes, config_merge and config_get read back */
        t = config_set("data = D:\\GOG\n", "presentation", "enhanced");
        CHECK(config_get(t, "presentation", val, sizeof val) && !strcmp(val, "enhanced"));
        CHECK(config_get(t, "data", val, sizeof val) && !strcmp(val, "D:\\GOG"));
        CHECK(!config_get(t, "scale", val, sizeof val));
        n = config_merge(t, NULL, 1, argv, &v, err, sizeof err);
        CHECK(n == 6);
        free(t);
        CHECK(config_get("a = \"q w\"\na = z\n", "a", val, sizeof val) && !strcmp(val, "z"));
        CHECK(config_get("a = \"q w\"\n", "a", val, sizeof val) && !strcmp(val, "q w"));
    }
    puts("f117a.ini options pass");
    return 0;
}
