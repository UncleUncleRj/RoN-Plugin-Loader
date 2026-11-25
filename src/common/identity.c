#include <string.h>
#include "common/identity.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

RonExeKind identity_kind(const uint8_t *image, uint32_t *timestamp_out)
{
    if (timestamp_out)
        *timestamp_out = 0;
    if (!image || image[0] != 'M' || image[1] != 'Z')
        return RON_EXE_UNKNOWN;
    uint32_t nt = rd32(image + 0x3C);
    if (nt < 0x40 || nt > 0x1000 || memcmp(image + nt, "PE\0\0", 4) != 0)
        return RON_EXE_UNKNOWN;
    const uint8_t *fh = image + nt + 4;  /* IMAGE_FILE_HEADER */
    uint32_t timestamp = rd32(fh + 4);
    if (timestamp_out)
        *timestamp_out = timestamp;
    if (rd16(fh) != 0x14C || timestamp != RON_EXE_TIMESTAMP)
        return RON_EXE_UNKNOWN;
    uint16_t sections = rd16(fh + 2);
    const uint8_t *sec = fh + 20 + rd16(fh + 16);
    for (uint16_t i = 0; i < sections && i < 96; i++, sec += 40) {
        if (memcmp(sec, ".cbp\0", 5) == 0)
            return RON_EXE_PATCHED;
    }
    return RON_EXE_STOCK;
}

uintptr_t identity_delta(const uint8_t *image)
{
    return (uintptr_t)image - RON_PREFERRED_BASE;
}
