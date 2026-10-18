#include "inputlog.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void)
{
    const char *path = "test-cadence.input";
    const char *headers[] = {
        "# f117r-input ips=40000000 time_us=700000000000000\nK 100 01\n",
        "# f117r-input ips=40000000 time_us=700000000000000\n# f117r-sim-hz 15\n# f117r-fixes D1REAL\nK 100 01\n",
        "# f117r-sim-hz 14\n", "# f117r-sim-hz 15garbage\n",
        "# f117r-sim-hz 15\n# f117r-sim-hz 8\n", "# f117r-sim-hz\n"
    };
    for (unsigned i = 0; i < sizeof headers / sizeof headers[0]; i++) {
        FILE *f = fopen(path, "w");
        CHECK(f);
        fputs(headers[i], f); fclose(f);
        unsigned hz = 99;
        int ok = inputlog_read_cadence(path, &hz);
        CHECK(ok == (i < 2));
        if (ok) CHECK(hz == (i == 0 ? 8 : 15));
    }
    FILE *f = fopen(path, "w");
    CHECK(f);
    inputlog_header(f, 40000000, 700000000000000ull);
    inputlog_cadence(f, 8); /* legacy records remain byte compatible */
    fclose(f);
    f = fopen(path, "r");
    CHECK(f);
    char text[256] = {0};
    fread(text, 1, sizeof text - 1, f); fclose(f);
    CHECK(!strcmp(text, "# f117r-input ips=40000000 time_us=700000000000000\n"));
    f = fopen(path, "a"); CHECK(f);
    inputlog_cadence(f, 15); fclose(f);
    unsigned hz;
    CHECK(inputlog_read_cadence(path, &hz) && hz == 15);
    remove(path);
    puts("recorded cadence and legacy defaults pass");
    return 0;
}
