#include <stdint.h>
#include <string.h>
#include "check.h"
#include "common/identity.h"
#include "synth_pe.h"

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
    CHECK_EQ(identity_kind(img, &ts), RON_EXE_PATCHED);
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
