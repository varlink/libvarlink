#include "interface.h"
#include "scanner.h"
#include "service.h"
#include "util.h"

#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

long varlink_type_new(VarlinkType **typep, const char *typestring) {
        _cleanup_(varlink_type_unrefp) VarlinkType *type = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;
        int r;

        r = scanner_new(&scanner, typestring, true);
        if (r < 0)
                return r;

        r = varlink_type_new_from_scanner(&type, scanner);
        if (r < 0)
                return r;

        if (scanner_peek(scanner) != '\0')
                return -VARLINK_ERROR_INVALID_TYPE;

        *typep = type;
        type = NULL;

        return 0;
}

long varlink_type_new_from_scanner(VarlinkType **typep, Scanner *scanner) {
        _cleanup_(varlink_type_unrefp) VarlinkType *type = NULL;
        long r;

        if (scanner_peek(scanner) == '[') {
                if (scanner_expect_operator(scanner, "[") < 0)
                        return -VARLINK_ERROR_INVALID_TYPE;

                if (scanner_read_keyword(scanner, "string")) {
                        r = varlink_type_allocate(&type, VARLINK_TYPE_MAP);
                        if (r < 0)
                                return r;
                } else {
                        r = varlink_type_allocate(&type, VARLINK_TYPE_ARRAY);
                        if (r < 0)
                                return r;
                }

                if (scanner_expect_operator(scanner, "]") < 0)
                        return -VARLINK_ERROR_INVALID_TYPE;

                r = varlink_type_new_from_scanner(&type->element_type, scanner);
                if (r < 0)
                        return r;

        } else if (scanner_peek(scanner) == '?') {
                if (scanner_expect_operator(scanner, "?") < 0)
                        return -VARLINK_ERROR_INVALID_TYPE;

                r = varlink_type_allocate(&type, VARLINK_TYPE_MAYBE);
                if (r < 0)
                        return r;

                r = varlink_type_new_from_scanner(&type->element_type, scanner);
                if (r < 0)
                        return r;

                /* Do not nest maybes */
                if (type->element_type->kind == VARLINK_TYPE_MAYBE)
                        return -VARLINK_ERROR_INVALID_TYPE;

        } else if (scanner_read_keyword(scanner, "bool")) {
                r = varlink_type_allocate(&type, VARLINK_TYPE_BOOL);
                if (r < 0)
                        return r;

        } else if (scanner_read_keyword(scanner, "int")) {
                r = varlink_type_allocate(&type, VARLINK_TYPE_INT);
                if (r < 0)
                        return r;

        } else if (scanner_read_keyword(scanner, "float")) {
                r = varlink_type_allocate(&type, VARLINK_TYPE_FLOAT);
                if (r < 0)
                        return r;

        } else if (scanner_read_keyword(scanner, "string")) {
                r = varlink_type_allocate(&type, VARLINK_TYPE_STRING);
                if (r < 0)
                        return r;

        } else if (scanner_read_keyword(scanner, "object")) {
                r = varlink_type_allocate(&type, VARLINK_TYPE_FOREIGN_OBJECT);
                if (r < 0)
                        return r;

        } else if (scanner_peek(scanner) ==  '(') {
                unsigned long n_fields_allocated = 0;

                if (scanner_expect_operator(scanner, "(") < 0)
                        return -VARLINK_ERROR_INVALID_TYPE;

                r = varlink_type_allocate(&type, VARLINK_TYPE_OBJECT);
                if (r < 0)
                        return r;

                for (unsigned long i = 0; scanner_peek(scanner) != ')'; i += 1) {
                        _cleanup_(varlink_type_field_freep) VarlinkTypeField *field = NULL;

                        if (i > 0)
                                if (scanner_expect_operator(scanner, ",") < 0)
                                        return -VARLINK_ERROR_INVALID_TYPE;

                        field = calloc(1, sizeof(VarlinkTypeField));
                        if (!field)
                                return -VARLINK_ERROR_PANIC;

                        r = scanner_get_last_docstring(scanner, &field->description);
                        if (r < 0)
                                return r;

                        r = scanner_expect_field_name(scanner, &field->name);
                        if (r < 0)
                                return r;

                        if (i == 0 && scanner_peek(scanner) != ':')
                                type->kind = VARLINK_TYPE_ENUM;

                        if (type->kind == VARLINK_TYPE_OBJECT) {
                                if (scanner_expect_operator(scanner, ":") < 0)
                                        return -VARLINK_ERROR_INVALID_TYPE;

                                r = varlink_type_new_from_scanner(&field->type, scanner);
                                if (r < 0)
                                        return r;
                        }

                        /* make sure a field with this name doesn't exist yet */
                        switch (avl_tree_insert(type->fields_sorted, field->name, field)) {
                                case 0:
                                        break;

                                case -AVL_ERROR_KEY_EXISTS:
                                        scanner_error(scanner, SCANNER_ERROR_DUPLICATE_FIELD_NAME);
                                        return -VARLINK_ERROR_INVALID_TYPE;

                                default:
                                        return -VARLINK_ERROR_PANIC;
                        }

                        if (type->n_fields == n_fields_allocated) {
                                n_fields_allocated = MAX(n_fields_allocated * 2, 4);
                                type->fields = realloc(type->fields, n_fields_allocated * sizeof(VarlinkTypeField *));
                                if (!type->fields)
                                        return -VARLINK_ERROR_PANIC;
                        }

                        type->fields[i] = field;
                        type->n_fields += 1;
                        field = NULL;
                }

                if (scanner_expect_operator(scanner, ")") < 0)
                        return -VARLINK_ERROR_INVALID_TYPE;

        } else {
                char *alias;

                r = scanner_expect_type_name(scanner, &alias);
                if (r < 0) {
                        scanner_error(scanner, SCANNER_ERROR_TYPE_EXPECTED);
                        return r;
                }

                varlink_type_allocate(&type, VARLINK_TYPE_ALIAS);
                type->alias = alias;
        }

        *typep = type;
        type = NULL;

        return 0;
}

static VarlinkTypeField *varlink_type_field_free(VarlinkTypeField *field) {
        if (field->name)
                free(field->name);

        if (field->type)
                varlink_type_unref(field->type);

        free(field->description);
        free(field);

        return NULL;
}

void varlink_type_field_freep(VarlinkTypeField **fieldp) {
        if (*fieldp)
                varlink_type_field_free(*fieldp);
}

VarlinkType *varlink_type_field_get_type(VarlinkType *type, const char *name) {
        VarlinkTypeField *field;

        assert(type->kind == VARLINK_TYPE_OBJECT);

        field = avl_tree_find(type->fields_sorted, name);
        if (!field)
                return NULL;

        return field->type;
}

static long field_compare(const void *key, void *value) {
        VarlinkTypeField *field = value;

        return strcmp(key, field->name);
}

long varlink_type_allocate(VarlinkType **typep,
                           VarlinkTypeKind kind) {
        VarlinkType *type;
        long r;

        type = calloc(1, sizeof(VarlinkType));
        if (!type)
                return -VARLINK_ERROR_PANIC;

        type->refcount = 1;
        type->kind = kind;

        if (kind == VARLINK_TYPE_OBJECT) {
                r = avl_tree_new(&type->fields_sorted, field_compare, NULL);
                if (r < 0)
                        return -VARLINK_ERROR_PANIC;
        }

        *typep = type;

        return 0;
}

VarlinkType *varlink_type_ref(VarlinkType *type) {
        type->refcount += 1;

        return type;
}

VarlinkType *varlink_type_unref(VarlinkType *type) {
        assert(type->refcount > 0);

        type->refcount -= 1;

        if (type->refcount == 0) {
                switch (type->kind) {
                        case VARLINK_TYPE_UNDEFINED:
                        case VARLINK_TYPE_BOOL:
                        case VARLINK_TYPE_INT:
                        case VARLINK_TYPE_FLOAT:
                        case VARLINK_TYPE_STRING:
                        case VARLINK_TYPE_FOREIGN_OBJECT:
                                break;

                        case VARLINK_TYPE_ENUM:
                        case VARLINK_TYPE_OBJECT:
                                for (unsigned long i = 0; i < type->n_fields; i += 1)
                                        varlink_type_field_free(type->fields[i]);

                                free(type->fields);
                                avl_tree_free(type->fields_sorted);
                                break;

                        case VARLINK_TYPE_ARRAY:
                        case VARLINK_TYPE_MAP:
                                if (type->element_type)
                                        varlink_type_unref(type->element_type);
                                break;

                        case VARLINK_TYPE_MAYBE:
                                if (type->element_type)
                                        varlink_type_unref(type->element_type);
                                break;

                        case VARLINK_TYPE_ALIAS:
                                free(type->alias);
                                break;
                }

                free(type->typestring);
                free(type);
        }

        return NULL;
}

void varlink_type_unrefp(VarlinkType **typep) {
        if (*typep)
                varlink_type_unref(*typep);
}

static bool is_multiline(VarlinkType *type) {
        /* "()" */
        if (type->n_fields == 0)
                return false;

        /* A maximum of two object fields */
        if (type->kind == VARLINK_TYPE_OBJECT && type->n_fields > 2)
                return true;

        for (unsigned long i = 0; i < type->n_fields; i += 1) {
                VarlinkTypeField *field = type->fields[i];

                /* No documentation */
                if (field->description)
                        return true;

                /* No nested complex types */
                if (type->kind == VARLINK_TYPE_OBJECT) {
                        if (field->type->kind == VARLINK_TYPE_OBJECT ||
                            field->type->kind == VARLINK_TYPE_ENUM)
                                return true;
                }
        }

        /* No longer than half a line */
        if (strlen(varlink_type_get_typestring(type)) > 40)
                return true;

        return false;
}

static long varlink_type_print(VarlinkType *type,
                               FILE *stream,
                               long indent,
                               const char *comment_pre, const char *comment_post,
                               const char *type_pre, const char *type_post) {
        long r;

        if (!type_pre)
                type_pre = "";

        if (!type_post)
                type_post = "";

        switch (type->kind) {
                case VARLINK_TYPE_UNDEFINED:
                        abort();

                case VARLINK_TYPE_BOOL:
                        if (fprintf(stream, "%sbool%s", type_pre, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_TYPE_INT:
                        if (fprintf(stream, "%sint%s", type_pre, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_TYPE_FLOAT:
                        if (fprintf(stream, "%sfloat%s", type_pre, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_TYPE_STRING:
                        if (fprintf(stream, "%sstring%s", type_pre, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_TYPE_ENUM:
                case VARLINK_TYPE_OBJECT: {
                        bool multiline = false;
                        bool docstring_printed = false;

                        if (indent >= 0)
                                multiline = is_multiline(type);

                        if (fprintf(stream, "(") < 0)
                                return -VARLINK_ERROR_PANIC;

                        for (unsigned long i = 0; i < type->n_fields; i += 1) {
                                VarlinkTypeField *field = type->fields[i];

                                if (multiline) {
                                        if (fprintf(stream, "\n") < 0)
                                                return -VARLINK_ERROR_PANIC;

                                        if (field->description) {
                                                if (i > 0 && !docstring_printed)
                                                        if (fprintf(stream, "\n") < 0)
                                                                return -VARLINK_ERROR_PANIC;

                                                for (const char *start = field->description; *start;) {
                                                        const char *end = strchrnul(start, '\n');
                                                        int len = end - start;

                                                        for (long l = 0; l < indent + 1; l += 1)
                                                                if (fprintf(stream, "  ") < 0)
                                                                        return -VARLINK_ERROR_PANIC;

                                                        if (fprintf(stream, "%s#", comment_pre) < 0)
                                                                return -VARLINK_ERROR_PANIC;

                                                        if (len > 0)
                                                                if (fprintf(stream, " %.*s", len, start) < 0)
                                                                        return -VARLINK_ERROR_PANIC;

                                                        if (fprintf(stream, "%s\n", comment_post) < 0)
                                                                return -VARLINK_ERROR_PANIC;

                                                        if (*end != '\n')
                                                                break;

                                                        start = end + 1;
                                                }

                                                docstring_printed = true;
                                        } else
                                                docstring_printed = false;

                                        for (long l = 0; l < indent + 1; l += 1)
                                                if (fprintf(stream, "  ") < 0)
                                                        return -VARLINK_ERROR_PANIC;
                                }

                                if (fprintf(stream, "%s", field->name) < 0)
                                        return -VARLINK_ERROR_PANIC;

                                if (type->kind == VARLINK_TYPE_OBJECT) {
                                        if (fprintf(stream, ": ") < 0)
                                                return -VARLINK_ERROR_PANIC;

                                        r = varlink_type_print(field->type,
                                                               stream,
                                                               indent >= 0 ? indent + 1 : -1,
                                                               comment_pre, comment_post,
                                                               type_pre, type_post);
                                        if (r < 0)
                                                return r;
                                }

                                if (i + 1 < type->n_fields) {
                                        if (fprintf(stream, ",") < 0)
                                                return -VARLINK_ERROR_PANIC;

                                        if (!multiline)
                                                if (fprintf(stream, " ") < 0)
                                                        return -VARLINK_ERROR_PANIC;

                                        if (multiline && field->description)
                                                if (fprintf(stream, "\n") < 0)
                                                        return -VARLINK_ERROR_PANIC;
                                }
                        }

                        if (multiline) {
                                if (fprintf(stream, "\n") < 0)
                                        return -VARLINK_ERROR_PANIC;

                                for (long l = 0; l < indent; l += 1)
                                        if (fprintf(stream, "  ") < 0)
                                                return -VARLINK_ERROR_PANIC;
                        }

                        if (fprintf(stream, ")") < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;
                }

                case VARLINK_TYPE_MAP:
                        if (fprintf(stream, "[%sstring%s]", type_pre, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;

                        r = varlink_type_print(type->element_type,
                                               stream,
                                               indent,
                                               comment_pre, comment_post,
                                               type_pre, type_post);
                        if (r < 0)
                                return r;

                        break;

                case VARLINK_TYPE_ARRAY:
                        if (fprintf(stream, "[]") < 0)
                                return -VARLINK_ERROR_PANIC;

                        r = varlink_type_print(type->element_type,
                                               stream,
                                               indent,
                                               comment_pre, comment_post,
                                               type_pre, type_post);
                        if (r < 0)
                                return r;

                        break;

                case VARLINK_TYPE_MAYBE:
                        if (fprintf(stream, "?") < 0)
                                return -VARLINK_ERROR_PANIC;

                        r = varlink_type_print(type->element_type,
                                               stream,
                                               indent,
                                               comment_pre, comment_post,
                                               type_pre, type_post);
                        if (r < 0)
                                return r;

                        break;

                case VARLINK_TYPE_FOREIGN_OBJECT:
                        if (fprintf(stream, "%sobject%s", type_pre, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                case VARLINK_TYPE_ALIAS:
                        if (fprintf(stream, "%s%s%s", type_pre, type->alias, type_post) < 0)
                                return -VARLINK_ERROR_PANIC;
                        break;

                default:
                        abort();
        }

        return 0;
}

const char *varlink_type_get_typestring(VarlinkType *type) {
        FILE *stream = NULL;
        _cleanup_(freep) char *string = NULL;
        size_t size;

        if (type->typestring)
                return type->typestring;

        stream = open_memstream(&type->typestring, &size);
        if (!stream)
                return NULL;

        if (varlink_type_print(type, stream, -1, NULL, NULL, NULL, NULL) < 0)
                return NULL;

        fclose(stream);

        return type->typestring;
}

long varlink_type_write_typestring(VarlinkType *type,
                                   FILE *stream,
                                   long indent,
                                   const char *comment_pre, const char *comment_post,
                                   const char *type_pre, const char *type_post) {
        if (!type_pre)
                type_pre = "";

        if (!type_post)
                type_post = "";

        return varlink_type_print(type,
                                  stream,
                                  indent,
                                  comment_pre, comment_post,
                                  type_pre, type_post);
}
