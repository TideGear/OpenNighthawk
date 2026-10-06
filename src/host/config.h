/* config.h - f117a.ini: the command-line options, kept in a file.
 *
 * Each key is a long option without its dashes and takes the option's value:
 *
 *   data = D:\GOG\F-117A
 *   mt32-control = roms\mt32_ctrl_1_07.rom
 *   fullscreen = yes
 *   fix = D4, D5
 *
 * ';' and '#' start a comment line, [sections] are ignored, a switch takes
 * yes/no (true/false, on/off, 1/0), "fix" takes a list, and a relative path
 * is taken from the file's own folder. The file's options are placed before
 * the command line's, so the command line wins. */
#ifndef F117R_CONFIG_H
#define F117R_CONFIG_H

#include <stddef.h>

/* The separator config_merge places between the file's options and the
 * command line's: the parser skips it and from then on knows which is which. */
#define CONFIG_CLI_MARK "\x01" "cli"

/* Read the INI text (its file's folder is dir, for relative paths; NULL for
 * none) and return argv[0], its options, CONFIG_CLI_MARK, then argv[1..].
 * The new array and its strings stay allocated for the program's life.
 * Returns the new argc, or -1 with a message in err. */
int config_merge(const char *text, const char *dir, int argc, char **argv,
                 char ***out_argv, char *err, size_t err_size);

/* The INI file to use: the value of --config on the command line, else
 * f117a.ini in base_dir if it exists, else NULL. *explicit_ is set when it
 * came from --config (so a missing file is an error). */
const char *config_path(int argc, char **argv, const char *base_dir, int *explicit_);

/* A whole file's text (NUL-terminated, malloc'd), or NULL. */
char *config_read_file(const char *path);

#endif
