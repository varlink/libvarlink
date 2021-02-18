// SPDX-License-Identifier: Apache-2.0

#include "array.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#include <float.h>
#include <inttypes.h>
#include <locale.h>

void varlink_value_clear(VarlinkValue *value) {
        switch (value->kind) {
                case VARLINK_VALUE_UNDEFINED:
                case VARLINK_VALUE_NULL:
                case VARLINK_VALUE_BOOL:
                case VARLINK_VALUE_INT:
                case VARLINK_VALUE_FLOAT:
                        break;

                case VARLINK_VALUE_STRING:
                        free(value->s);
                        break;

                case VARLINK_VALUE_ARRAY:
                        if (value->array)
                                varlink_array_unref(value->array);
                        break;

                case VARLINK_VALUE_OBJECT:
                        if (value->object)
                                varlink_object_unref(value->object);
                        break;
        }
}

long varlink_value_read_from_scanner(VarlinkValue *value, Scanner *scanner) {
        ScannerNumber number;
        long r;

        if (scanner_peek(scanner) == '{') {
                r = varlink_object_new_from_scanner(&value->object, scanner);
                if (r < 0)
                        return false;

                value->kind = VARLINK_VALUE_OBJECT;

        } else if (scanner_peek(scanner) == '[') {
                r = varlink_array_new_from_scanner(&value->array, scanner);
                if (r < 0)
                        return false;

                value->kind = VARLINK_VALUE_ARRAY;

        } else if (scanner_read_keyword(scanner, "null")) {
                value->kind = VARLINK_VALUE_NULL;

        } else if (scanner_read_keyword(scanner, "true")) {
                value->b = true;
                value->kind = VARLINK_VALUE_BOOL;

        } else if (scanner_read_keyword(scanner, "false")) {
                value->b = false;
                value->kind = VARLINK_VALUE_BOOL;

        } else if (scanner_peek(scanner) == '"') {
                r = scanner_expect_string(scanner, &value->s);
                if (r < 0)
                        return r;

                value->kind = VARLINK_VALUE_STRING;

        } else if (scanner_read_number(scanner, &number)) {
                if (number.is_double) {
                        value->f = number.d;
                        value->kind = VARLINK_VALUE_FLOAT;
                } else {
                        value->i = number.i;
                        value->kind = VARLINK_VALUE_INT;
                }

        } else {
                scanner_error(scanner, SCANNER_ERROR_JSON_EXPECTED);
                return false;
        }

        return true;
}

static long json_write_string(FILE *stream, const char *s) {
        while (*s != '\0') {
                switch(*s) {
                        case '\"':
                                if (fprintf(stream, "\\\"") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        case '\\':
                                if (fprintf(stream, "\\\\") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        case '\b':
                                if (fprintf(stream, "\\b") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        case '\f':
                                if (fprintf(stream, "\\f") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        case '\n':
                                if (fprintf(stream, "\\n") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        case '\r':
                                if (fprintf(stream, "\\r") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        case '\t':
                                if (fprintf(stream, "\\t") < 0)
                                        return -VARLINK_ERROR_PANIC;
                                break;

                        default:
                                if (*(uint8_t *)s < 0x20) {
                                        if (fprintf(stream, "\\u%04x", *s) < 0)
                                                return -VARLINK_ERROR_PANIC;
                                } else {
                                        if (fprintf(stream, "%c", *s) < 0)
                                                return -VARLINK_ERROR_PANIC;
                                }
                }

                s += 1;
        }

        return 0;
}

long varlink_value_write_json(VarlinkValue *value,
                              FILE *stream,
                              long indent,
                              const char *key_pre, const char *key_post,
                              const char *value_pre, const char *value_post) {
        long r;

        switch (value->kind) {
                case VARLINK_VALUE_UNDEFINED:
                        abort();

                case VARLINK_VALUE_NULL:
                        if (fprintf(stream, "%snull%s", value_pre, value_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_VALUE_BOOL:
                        if (fprintf(stream, "%s%s%s", value_pre, value->b ? "true" : "false", value_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_VALUE_INT:
                        if (fprintf(stream, "%s%" PRIi64 "%s", value_pre, value->i, value_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_VALUE_FLOAT: {
                        locale_t loc;

                        loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t) 0);

                        if (fprintf(stream, "%s%.*e%s", value_pre, DECIMAL_DIG, value->f, value_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        freelocale(loc);
                        break;
                }

                case VARLINK_VALUE_STRING:
                        if (fprintf(stream, "\"%s", value_pre) < 0)
                                return -VARLINK_ERROR_PANIC;

                        r = json_write_string(stream, value->s);
                        if (r < 0)
                                return r;

                        if (fprintf(stream, "%s\"", value_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_VALUE_ARRAY:
                        r = varlink_array_write_json(value->array, stream, indent,
                                                     key_pre, key_post, value_pre, value_post);
                        if (r < 0)
                                return r;
                        break;

                case VARLINK_VALUE_OBJECT:
                        r = varlink_object_write_json(value->object, stream, indent,
                                                      key_pre, key_post, value_pre, value_post);
                        if (r < 0)
                                return r;
                        break;

        }

        return 0;
}
