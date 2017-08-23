#include "value.h"

#include "array.h"
#include "string.h"
#include "object.h"
#include "util.h"

#include <float.h>
#include <inttypes.h>
#include <locale.h>

void varlink_value_clear(VarlinkTypeKind kind, VarlinkValue *value) {
        switch (kind) {
                case VARLINK_TYPE_BOOL:
                case VARLINK_TYPE_INT:
                case VARLINK_TYPE_FLOAT:
                        break;

                case VARLINK_TYPE_STRING:
                        free(value->s);
                        break;

                case VARLINK_TYPE_ARRAY:
                        if (value->array)
                                varlink_array_unref(value->array);
                        break;

                case VARLINK_TYPE_OBJECT:
                case VARLINK_TYPE_FOREIGN_OBJECT:
                        if (value->object)
                                varlink_object_unref(value->object);
                        break;

                case VARLINK_TYPE_ENUM:
                case VARLINK_TYPE_ALIAS:
                        abort();
                        break;
        }
}

bool varlink_value_read_from_scanner(VarlinkTypeKind *kindp, VarlinkValue *value, Scanner *scanner) {
        ScannerNumber number;

        if (scanner_peek(scanner) == '{') {
                if (!varlink_object_new_from_scanner(&value->object, scanner))
                        return false;
                *kindp = VARLINK_TYPE_OBJECT;

        } else if (scanner_peek(scanner) == '[') {
                if (!varlink_array_new_from_scanner(&value->array, scanner))
                        return false;

                *kindp = VARLINK_TYPE_ARRAY;

        } else if (scanner_read_keyword(scanner, "true")) {
                value->b = true;
                *kindp = VARLINK_TYPE_BOOL;

        } else if (scanner_read_keyword(scanner, "false")) {
                value->b = false;
                *kindp = VARLINK_TYPE_BOOL;

        } else if (scanner_read_string(scanner, &value->s)) {
                *kindp = VARLINK_TYPE_STRING;

        } else if (scanner_read_number(scanner, &number)) {
                if (number.is_double) {
                        value->f = number.d;
                        *kindp = VARLINK_TYPE_FLOAT;
                } else {
                        value->i = number.i;
                        *kindp = VARLINK_TYPE_INT;
                }
        } else
                return scanner_error(scanner, "json value expected");

        return true;
}

static void json_write_string(FILE *stream, const char *s) {
        while (*s != '\0') {
                switch(*s) {
                        case '\"':
                                fprintf(stream, "\\\"");
                                break;

                        case '\\':
                                fprintf(stream, "\\\\");
                                break;

                        case '\b':
                                fprintf(stream, "\\b");
                                break;

                        case '\f':
                                fprintf(stream, "\\f");
                                break;

                        case '\n':
                                fprintf(stream, "\\n");
                                break;

                        case '\r':
                                fprintf(stream, "\\r");
                                break;

                        case '\t':
                                fprintf(stream, "\\t");
                                break;

                        default:
                                if (*(uint8_t *)s < 0x20)
                                        fprintf(stream, "\\u%04x", *s);
                                else
                                        fprintf(stream, "%c", *s);
                }

                s += 1;
        }
}

long varlink_value_write_json(VarlinkTypeKind kind,
                              VarlinkValue *value,
                              FILE *stream,
                              long indent,
                              const char *key_pre, const char *key_post,
                              const char *value_pre, const char *value_post) {
        long r;

        switch (kind) {
                case VARLINK_TYPE_BOOL:
                        fprintf(stream, "%s%s%s", value_pre, value->b ? "true" : "false", value_post);
                        break;

                case VARLINK_TYPE_INT:
                        fprintf(stream, "%s%" PRIi64 "%s", value_pre, value->i, value_post);
                        break;

                case VARLINK_TYPE_FLOAT: {
                        locale_t loc;

                        loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t) 0);

                        fprintf(stream, "%s%.*e%s", value_pre, DECIMAL_DIG, value->f, value_post);
                        freelocale(loc);
                        break;
                }

                case VARLINK_TYPE_STRING:
                        fprintf(stream, "\"%s", value_pre);
                        json_write_string(stream, value->s);
                        fprintf(stream, "%s\"", value_post);
                        break;

                case VARLINK_TYPE_ARRAY:
                        r = varlink_array_write_json(value->array, stream, indent,
                                                     key_pre, key_post, value_pre, value_post);
                        if (r < 0)
                                return r;
                        break;

                case VARLINK_TYPE_OBJECT:
                case VARLINK_TYPE_FOREIGN_OBJECT:
                        r = varlink_object_write_json(value->object, stream, indent,
                                                      key_pre, key_post, value_pre, value_post);
                        if (r < 0)
                                return r;
                        break;

                case VARLINK_TYPE_ENUM:
                case VARLINK_TYPE_ALIAS:
                        abort();
                        break;
        }

        return 0;
}
