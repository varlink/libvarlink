// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <locale.h>
#include "scanner.h"
#include "value.h"
#include "varlink.h"

#include <stdio.h>

long varlink_object_new_from_scanner(VarlinkObject **objectp, Scanner *scanner, locale_t locale,
                                     unsigned long depth_cnt);

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
