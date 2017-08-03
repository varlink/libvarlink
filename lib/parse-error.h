#pragma once

#include "varlink.h"

typedef struct VarlinkParseError VarlinkParseError;

struct VarlinkParseError {
        char *message;
        unsigned long line_nr;
        unsigned long pos_nr;
};

long varlink_parse_error_new(VarlinkParseError **errorp);
VarlinkParseError *varlink_parse_error_free(VarlinkParseError *error);
void varlink_parse_error_freep(VarlinkParseError **errorp);
const char *varlink_parse_error_get_string(VarlinkParseError *error, unsigned long *lin_nrp, unsigned long *pos_nrp);
