/* Runs every registered test (optionally only those whose name contains argv[1]). */
#include "check.h"

#define MAX_TESTS 512

static struct { const char *name; TestFn fn; } g_tests[MAX_TESTS];
static int g_count;
int g_test_failed;

void test_register(const char *name, TestFn fn)
{
    if (g_count < MAX_TESTS) {
        g_tests[g_count].name = name;
        g_tests[g_count].fn = fn;
        g_count++;
    }
}

int main(int argc, char **argv)
{
    int passed = 0, failed = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; i < g_count; i++) {
        if (argc > 1 && !strstr(g_tests[i].name, argv[1]))
            continue;
        g_test_failed = 0;
        g_tests[i].fn();
        if (g_test_failed) {
            printf("FAIL %s\n", g_tests[i].name);
            failed++;
        } else {
            passed++;
        }
    }
    printf("tests: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
