#include "varlink.h"

#include <assert.h>

static void test_error_strings(void) {
        for (long i = 1 ; i < VARLINK_ERROR_MAX; i += 1) {
                const char *string = varlink_error_string(i);

                assert(string != NULL);
                assert(string[0] != '<');
        }
}

int main(void) {
        test_error_strings();
}
