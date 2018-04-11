#pragma once

#include "scanner.h"
#include "value.h"
#include "varlink.h"

#include <stdio.h>

typedef enum {
        VARLINK_VALUE_BOOL = 0,
        VARLINK_VALUE_INT,
        VARLINK_VALUE_FLOAT,
        VARLINK_VALUE_STRING,
        VARLINK_VALUE_ARRAY,
        VARLINK_VALUE_OBJECT
} VarlinkValueKind;

typedef union {
        bool b;
        int64_t i;
        double f;
        char *s;
        VarlinkArray *array;
        VarlinkObject *object;
} VarlinkValue;

long varlink_value_read_from_scanner(VarlinkValueKind *kindp, VarlinkValue *value, Scanner *scanner);
long varlink_value_write_json(VarlinkValueKind kind,
                              VarlinkValue *value,
                              FILE *stream,
                              long indent,
                              const char *key_pre, const char *key_post,
                              const char *value_pre, const char *value_post);

void varlink_value_clear(VarlinkValueKind kind, VarlinkValue *value);
