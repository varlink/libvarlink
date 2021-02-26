// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include "varlink.h"
#include "util.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <glob.h>
#include <errno.h>

static int read_file(FILE *file, char **contentsp) {
        _cleanup_ (fclosep) FILE *stream = NULL;
        _cleanup_ (freep) char *contents = NULL;
        size_t n_input;

        stream = open_memstream(&contents, &n_input);
        if (!stream)
                return -errno;

        fputs("{ \"test\" : ", stream);

        for (;;) {
                char buffer[8192];
                size_t n;

                n = fread(buffer, 1, sizeof(buffer), file);
                if (n == 0) {
                        if (ferror(file))
                                return -errno;

                        break;
                }

                if (fwrite(buffer, 1, n, stream) != n)
                        return -errno;
        }

        fputs("}", stream);
        fclose(stream);
        stream = NULL;

        *contentsp = contents;
        contents = NULL;

        return 0;
}

int main(int argc, char **argv) {
        _cleanup_(freep) char *input = NULL;
        _cleanup_(fclosep) FILE *file = NULL;
        VarlinkObject *s;

        assert(argc != 1);

        file = fopen(argv[1], "r");
        assert(file != NULL);
        assert(read_file(file, &input) == 0);
        assert(varlink_object_new_from_json(&s, input) == 0);

        return EXIT_SUCCESS;
}
