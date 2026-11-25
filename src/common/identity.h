/* Which game exe a mapped image is: stock riseofnations.exe, CBP's riseofnations_CBP.exe, or neither. */
#ifndef RON_IDENTITY_H
#define RON_IDENTITY_H

#include <stdint.h>
#include "ron_plugin.h"

#define RON_EXE_TIMESTAMP 0x6674863Fu
#define RON_PREFERRED_BASE 0x400000u

/* Reads the DOS and NT headers of a mapped image. *timestamp_out gets the PE TimeDateStamp
   (0 when the headers are not a PE). */
RonExeKind identity_kind(const uint8_t *image, uint32_t *timestamp_out);

/* image - 0x400000: the ASLR load delta every static address is adjusted by. */
uintptr_t identity_delta(const uint8_t *image);

#endif
