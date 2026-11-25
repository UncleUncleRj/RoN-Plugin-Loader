#include <stdint.h>
#include <string.h>
#include "check.h"
#include "common/identity.h"

/* A 0x400-byte image: MZ, e_lfanew 0x40, PE\0\0, file header, a 0xE0 optional header, two sections. */
static void make_image(uint8_t *img, uint32_t timestamp, const char *sec2)
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

TEST(identity_stock)
{
    static uint8_t img[0x400];
    uint32_t ts = 0;
    make_image(img, 0x6674863F, ".rdata");
    CHECK_EQ(identity_kind(img, &ts), RON_EXE_STOCK);
    CHECK_EQ(ts, 0x6674863F);
}

TEST(identity_cbp)
{
    static uint8_t img[0x400];
    uint32_t ts = 0;
    make_image(img, 0x6674863F, ".cbp");
    CHECK_EQ(identity_kind(img, &ts), RON_EXE_CBP);
}

TEST(identity_other_build)
{
    static uint8_t img[0x400];
    uint32_t ts = 0;
    make_image(img, 0x66748640, ".rdata");
    CHECK_EQ(identity_kind(img, &ts), RON_EXE_UNKNOWN);
    CHECK_EQ(ts, 0x66748640);
}

TEST(identity_bad_mz)
{
    static uint8_t img[0x400];
    uint32_t ts = 0;
    make_image(img, 0x6674863F, ".rdata");
    img[0] = 'X';
    CHECK_EQ(identity_kind(img, &ts), RON_EXE_UNKNOWN);
}

TEST(identity_delta_zero)
{
    CHECK_EQ(identity_delta((const uint8_t *)0x400000), 0);
    CHECK_EQ(identity_delta((const uint8_t *)0x410000), 0x10000);
}
