/* Minimal check harness shared by the host tests. Counts, reports the first
 * line that failed, and makes the exit code mean something to CI. */
#pragma once

#include <stdio.h>
#include <stdlib.h>

static int phm_checks = 0;
static int phm_fails = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        phm_checks++;                                     \
        if(!(cond)) {                                     \
            phm_fails++;                                  \
            if(phm_fails < 40) {                          \
                printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
                printf(__VA_ARGS__);                      \
                printf("\n");                             \
            }                                             \
        }                                                 \
    } while(0)

#define CHECK_STR(got, want)                                        \
    do {                                                            \
        phm_checks++;                                               \
        if(strcmp((got), (want)) != 0) {                            \
            phm_fails++;                                            \
            printf("  FAIL %s:%d  got \"%s\", want \"%s\"\n",       \
                   __FILE__, __LINE__, (got), (want));              \
        }                                                           \
    } while(0)

#define CHECK_RANGE(v, lo, hi, name)                                          \
    do {                                                                      \
        long _v = (long)(v);                                                  \
        CHECK(_v >= (long)(lo) && _v <= (long)(hi), "%s = %ld, want %ld..%ld", \
              name, _v, (long)(lo), (long)(hi));                              \
    } while(0)

static int phm_report(const char* suite) {
    printf("%s: %d checks, %d failed\n", suite, phm_checks, phm_fails);
    return phm_fails ? 1 : 0;
}
