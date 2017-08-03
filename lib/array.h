#pragma once

#include "scanner.h"
#include "varlink.h"
#include "value.h"

long varlink_array_new_from_scanner(VarlinkArray **arrayp, Scanner *scanner);
long varlink_array_get_value(VarlinkArray *array, unsigned long index, VarlinkValue **valuep);
VarlinkTypeKind varlink_array_get_element_kind(VarlinkArray *array);
long varlink_array_write_json(VarlinkArray *array,
                              FILE *stream,
                              long indent,
                              const char *key_pre, const char *key_post,
                              const char *value_pre, const char *value_post);
