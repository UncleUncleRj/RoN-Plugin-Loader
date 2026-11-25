/* Whether shfolder.dll does anything in this process: only in the known game exe. */
#ifndef RON_DECIDE_H
#define RON_DECIDE_H

#include <stdint.h>

int bs_should_run(const uint8_t *image);   /* 1 for the stock or CBP exe, 0 otherwise */

#endif
