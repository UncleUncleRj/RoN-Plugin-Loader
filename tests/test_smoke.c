#include "check.h"
#include "ron_plugin.h"

TEST(smoke_header)
{
    CHECK_EQ(RON_API_MAJOR, 1);
    CHECK_EQ(RON_EV_COUNT, 7);
    CHECK_EQ(sizeof(void *), 4);
}
