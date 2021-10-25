#include <stdlib.h>
#include "naomi/maple.h"

void test_maple(test_context_t *context)
{
    ASSERT(maple_request_self_test(), "Maple chip reports bad RAM!");

    char version[128];
    memset(version, 0, 128);
    maple_request_version(version);

    ASSERT(strstr(version, "315-6149") != 0, "Version string \"%s\" missing part number!", version);
    ASSERT(strstr(version, "SEGA") != 0, "Version string \"%s\" missing copyright!", version);
}
