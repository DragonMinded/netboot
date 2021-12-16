// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/utf8.h"

void test_console(test_context_t *context)
{
    int first;
    int second;

    // First thing first, save the position so we can restore it at the end
    // of this test.
    printf("%c7", 0x1B);

    // Grab what console type we are.
    printf("%c[0c", 0x1B);
    fflush(stdout);

    ASSERT(scanf("\033[?%d;%dc", &first, &second) == 2, "Did not retrieve full console type!");
    ASSERT(first == 1, "Unexpected response from console type request!");
    ASSERT(second == 0, "Unexpected capabilities from console type request!");

    // Try it again with the alternate request sequence.
    printf("%c[c", 0x1B);
    fflush(stdout);

    ASSERT(scanf("\033[?%d;%dc", &first, &second) == 2, "Did not retrieve full console type!");
    ASSERT(first == 1, "Unexpected response from console type request!");
    ASSERT(second == 0, "Unexpected capabilities from console type request!");

    // Now, verify that the console resports that it is doing okay.
    printf("%c[5n", 0x1B);
    fflush(stdout);

    ASSERT(scanf("\033[%dn", &first) == 1, "Did not retrieve full console status!");
    ASSERT(first == 0, "Unexpected response from console status request!");

    // Now, get the current console position, so we can manipulate it and verify
    // that it seems legit.
    printf("%c[6n", 0x1B);
    fflush(stdout);

    int origrow;
    int origcol;
    ASSERT(scanf("\033[%d;%dR", &origrow, &origcol) == 2, "Did not retrieve full console position!");

    printf("\n");
    fflush(stdout);

    printf("%c[6n", 0x1B);
    fflush(stdout);

    int newrow;
    int newcol;
    ASSERT(scanf("\033[%d;%dR", &newrow, &newcol) == 2, "Did not retrieve full console position!");
    ASSERT(newrow == origrow + 1, "Did not see console move down a line!");
    ASSERT(newcol == 1, "Did not see console move to home column!");

    printf(" ");
    fflush(stdout);

    printf("%c[6n", 0x1B);
    fflush(stdout);

    ASSERT(scanf("\033[%d;%dR", &newrow, &newcol) == 2, "Did not retrieve full console position!");
    ASSERT(newrow == origrow + 1, "Did not see console row remain the same!");
    ASSERT(newcol == 2, "Did not see console move right a single character!");

    // Finally, go back to where we started so we can let the test stub
    // overwrite our garbage.
    printf("%c8%c[J", 0x1B, 0x1B);

    // Verify that it was rolled back.
    printf("%c[6n", 0x1B);
    fflush(stdout);

    ASSERT(scanf("\033[%d;%dR", &newrow, &newcol) == 2, "Did not retrieve full console position!");
    ASSERT(newrow == origrow, "Console was not restored!");
    ASSERT(newcol == origcol, "Console was not restored!");
}
