#pragma once

#include "interface.h"
#include "scanner.h"
#include "varlink.h"

#include <stdio.h>

typedef union VarlinkValue VarlinkValue;

union VarlinkValue {
        bool b;
        int64_t i;
        double f;
        char *s;
        VarlinkArray *array;
        VarlinkObject *object;
};

bool varlink_value_read_from_scanner(VarlinkTypeKind *kindp, VarlinkValue *value, Scanner *scanner);
long varlink_value_write_json(VarlinkTypeKind kind,
                              VarlinkValue *value,
                              FILE *stream,
                              long indent,
                              const char *key_pre, const char *key_post,
                              const char *value_pre, const char *value_post);

void varlink_value_clear(VarlinkTypeKind kind, VarlinkValue *value);
