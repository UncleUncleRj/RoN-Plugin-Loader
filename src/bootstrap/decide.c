#include "bootstrap/decide.h"
#include "common/identity.h"

int bs_should_run(const uint8_t *image)
{
    return identity_kind(image, NULL) != RON_EXE_UNKNOWN;
}
