/* A 0x400-byte PE image for identity tests: MZ, e_lfanew 0x40, PE\0\0, file header, a 0xE0
   optional header, two sections (.text and sec2). */
#ifndef SYNTH_PE_H
#define SYNTH_PE_H

#include <stdint.h>
#include <string.h>

static inline void make_image(uint8_t *img, uint32_t timestamp, const char *sec2)
{
    memset(img, 0, 0x400);
    img[0] = 'M';
    img[1] = 'Z';
    *(uint32_t *)(img + 0x3C) = 0x40;
    memcpy(img + 0x40, "PE\0\0", 4);
    *(uint16_t *)(img + 0x44) = 0x14C;  /* Machine */
    *(uint16_t *)(img + 0x46) = 2;      /* NumberOfSections */
    *(uint32_t *)(img + 0x48) = timestamp;
    *(uint16_t *)(img + 0x54) = 0xE0;   /* SizeOfOptionalHeader */
    memcpy(img + 0x58 + 0xE0, ".text", 5);
    memcpy(img + 0x58 + 0xE0 + 40, sec2, strlen(sec2));
}

#endif
