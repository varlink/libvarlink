#pragma once

#include "scanner.h"
#include "varlink.h"
#include "value.h"

long varlink_object_new_from_type(VarlinkObject **objectp, VarlinkType *type);
bool varlink_object_new_from_scanner(VarlinkObject **objectp, Scanner *scanner);

long varlink_object_write_json(VarlinkObject *object,
                               FILE *stream,
                               long indent,
                               const char *key_pre, const char *key_post,
                               const char *value_pre, const char *value_post);

long varlink_object_to_pretty_json(VarlinkObject *object,
                                   char **stringp,
                                   long indent,
                                   const char *key_pre, const char *key_post,
                                   const char *value_pre, const char *value_post);

long varlink_object_set_empty_object(VarlinkObject *object, const char *field);
