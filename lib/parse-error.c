#include "parse-error.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long varlink_parse_error_new(VarlinkParseError **errorp) {
        *errorp = calloc(1, sizeof(VarlinkParseError));

        return 0;
}

VarlinkParseError *varlink_parse_error_free(VarlinkParseError *error) {
        free(error->message);
        free(error);

        return NULL;
}

void varlink_parse_error_freep(VarlinkParseError **errorp) {
        if (*errorp)
                varlink_parse_error_free(*errorp);
}

const char *varlink_parse_error_get_string(VarlinkParseError *error, unsigned long *line_nrp, unsigned long *pos_nrp) {
        if (line_nrp)
                *line_nrp = error->line_nr;

        if (pos_nrp)
                *pos_nrp = error->pos_nr;

        return error->message;
}
