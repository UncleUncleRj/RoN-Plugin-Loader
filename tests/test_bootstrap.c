#include "check.h"
#include "synth_pe.h"
#include "bootstrap/decide.h"

TEST(bs_runs_stock)
{
    static uint8_t img[0x400];
    make_image(img, 0x6674863F, ".rdata");
    CHECK_EQ(bs_should_run(img), 1);
}

TEST(bs_runs_cbp)
{
    static uint8_t img[0x400];
    make_image(img, 0x6674863F, ".cbp");
    CHECK_EQ(bs_should_run(img), 1);
}

TEST(bs_skips_other_build)
{
    static uint8_t img[0x400];
    make_image(img, 0x66748640, ".rdata");
    CHECK_EQ(bs_should_run(img), 0);
}
