/* A minimal self-registering test harness. TEST(name) { CHECK(...); } */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>

typedef void (*TestFn)(void);
void test_register(const char *name, TestFn fn);
extern int g_test_failed;

#define TEST(name)                                                                         \
    static void name(void);                                                                \
    __attribute__((constructor)) static void name##_register(void) { test_register(#name, name); } \
    static void name(void)

#define CHECK(c)                                                                           \
    do {                                                                                   \
        if (!(c)) {                                                                        \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
            g_test_failed = 1;                                                             \
        }                                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        long long check_a_ = (long long)(a), check_b_ = (long long)(b);                    \
        if (check_a_ != check_b_) {                                                        \
            printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b,  \
                   check_a_, check_b_);                                                    \
            g_test_failed = 1;                                                             \
        }                                                                                  \
    } while (0)

#define CHECK_STR(a, b)                                                                    \
    do {                                                                                   \
        const char *check_a_ = (a), *check_b_ = (b);                                       \
        if (!check_a_ || !check_b_ || strcmp(check_a_, check_b_) != 0) {                   \
            printf("  FAIL %s:%d: %s == %s (\"%s\" vs \"%s\")\n", __FILE__, __LINE__, #a,  \
                   #b, check_a_ ? check_a_ : "(null)", check_b_ ? check_b_ : "(null)");    \
            g_test_failed = 1;                                                             \
        }                                                                                  \
    } while (0)

#endif
