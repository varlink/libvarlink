// SPDX-License-Identifier: Apache-2.0

#include "array.h"
#include "avltree.h"
#include "object.h"
#include "scanner.h"
#include "util.h"

#include <inttypes.h>
#include <string.h>
#include <locale.h>

typedef struct Field Field;

struct VarlinkObject {
        unsigned long refcount;
        AVLTree *fields;
        bool writable;
};

struct Field {
        char *name;
        VarlinkValue value;
};

static long field_compare(const void *key, void *value) {
        Field *field = value;

        return strcmp(key, field->name);
}

static void field_freep(void *ptr) {
        Field *field = *(void **)ptr;

        free(field->name);
        varlink_value_clear(&field->value);
        free(field);
}

static long object_add_field(VarlinkObject *object, const char *name, Field **fieldp) {
        _cleanup_(freep) Field *field = NULL;
        long r;

        field = calloc(1, sizeof(Field));
        if (!field)
                return -VARLINK_ERROR_PANIC;

        field->name = strdup(name);
        if (!field->name)
                return -VARLINK_ERROR_PANIC;

        r = avl_tree_insert(object->fields, field->name, field);
        if (r < 0)
                return -VARLINK_ERROR_PANIC;

        *fieldp = field;
        field = NULL;

        return 0;
}

static void object_remove_field(VarlinkObject *object, const char *name) {
        avl_tree_remove(object->fields, name);
}

_public_ long varlink_object_new(VarlinkObject **objectp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *object = NULL;
        long r;

        object = calloc(1, sizeof(VarlinkObject));
        if (!object)
                return -VARLINK_ERROR_PANIC;

        object->refcount = 1;
        object->writable = true;
        r = avl_tree_new(&object->fields, field_compare, field_freep);
        if (r < 0)
                return -VARLINK_ERROR_PANIC;

        *objectp = object;
        object = NULL;

        return 0;
}

long varlink_object_new_from_scanner(VarlinkObject **objectp, Scanner *scanner, locale_t locale) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *object = NULL;
        bool first = true;
        long r;

        if (scanner_expect_operator(scanner, "{") < 0)
                return -VARLINK_ERROR_INVALID_JSON;

        r = varlink_object_new(&object);
        if (r < 0)
                return r;

        while (scanner_peek(scanner) != '}') {
                _cleanup_(freep) char *name = NULL;
                Field *field;

                if (!first) {
                        if (scanner_expect_operator(scanner, ",") < 0)
                                return -VARLINK_ERROR_INVALID_JSON;
                }

                r = scanner_expect_string(scanner, &name);
                if (r < 0)
                        return r;

                if (scanner_expect_operator(scanner, ":") < 0)
                        return -VARLINK_ERROR_INVALID_JSON;

                r = object_add_field(object, name, &field);
                if (r < 0)
                        return r;

                if (!varlink_value_read_from_scanner(&field->value, scanner, locale))
                        return -VARLINK_ERROR_INVALID_JSON;

                /* Treat `null` the same as non-existent keys */
                if (field->value.kind == VARLINK_VALUE_NULL)
                        object_remove_field(object, name);

                first = false;
        }

        if (scanner_expect_operator(scanner, "}") < 0)
                return -VARLINK_ERROR_INVALID_JSON;

        *objectp = object;
        object = NULL;

        return 0;
}

_public_ long varlink_object_new_from_json(VarlinkObject **objectp, const char *json) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *object = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;
        long r;
        locale_t new_locale;

        r = scanner_new(&scanner, json, false);
        if (r < 0)
                return r;

        new_locale = newlocale(LC_NUMERIC_MASK, "C",  (locale_t) 0);

        if (new_locale == (locale_t) 0)
                return -VARLINK_ERROR_PANIC;

        r = varlink_object_new_from_scanner(&object, scanner, new_locale);

        freelocale(new_locale);

        if (r < 0)
                return r;

        if (scanner_peek(scanner) != '\0')
                return -VARLINK_ERROR_INVALID_JSON;

        *objectp = object;
        object = NULL;

        return 0;
}

_public_ VarlinkObject *varlink_object_ref(VarlinkObject *object) {
        object->refcount += 1;
        return object;
}

_public_ VarlinkObject *varlink_object_unref(VarlinkObject *object) {
        object->refcount -= 1;

        if (object->refcount == 0) {
                avl_tree_free(object->fields);
                free(object);
        }

        return NULL;
}

_public_ void varlink_object_unrefp(VarlinkObject **objectp) {
        if (*objectp)
                varlink_object_unref(*objectp);
}

_public_ long varlink_object_get_field_names(VarlinkObject *object, const char ***namesp) {
        unsigned long n_fields;

        n_fields = avl_tree_get_n_elements(object->fields);

        if (namesp) {
                _cleanup_(freep) const char **names = NULL;
                AVLTreeNode *node;
                unsigned long i = 0;

                names = calloc(n_fields + 1, sizeof(const char *));
                if (!names)
                        return -VARLINK_ERROR_PANIC;

                node = avl_tree_first(object->fields);
                while (node) {
                        Field *field = avl_tree_node_get(node);

                        names[i] = field->name;

                        node = avl_tree_node_next(node);
                        i += 1;
                }

                *namesp = names;
                names = NULL;
        }

        return n_fields;
}

_public_ long varlink_object_get_bool(VarlinkObject *object, const char *field_name, bool *bp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->value.kind != VARLINK_VALUE_BOOL)
                return -VARLINK_ERROR_INVALID_TYPE;

        *bp = field->value.b;

        return 0;
}

_public_ long varlink_object_get_int(VarlinkObject *object, const char *field_name, int64_t *ip) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->value.kind != VARLINK_VALUE_INT)
                return -VARLINK_ERROR_INVALID_TYPE;

        *ip = field->value.i;

        return 0;
}

_public_ long varlink_object_get_float(VarlinkObject *object, const char *field_name, double *fp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->value.kind == VARLINK_VALUE_INT)
                *fp = field->value.i;
        else if (field->value.kind == VARLINK_VALUE_FLOAT)
                *fp = field->value.f;
        else
                return -VARLINK_ERROR_INVALID_TYPE;

        return 0;
}

_public_ long varlink_object_get_string(VarlinkObject *object, const char *field_name, const char **stringp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->value.kind != VARLINK_VALUE_STRING)
                return -VARLINK_ERROR_INVALID_TYPE;

        *stringp = field->value.s;

        return 0;
}

_public_ long varlink_object_get_array(VarlinkObject *object, const char *field_name, VarlinkArray **arrayp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->value.kind != VARLINK_VALUE_ARRAY)
                return -VARLINK_ERROR_INVALID_TYPE;

        *arrayp = field->value.array;

        return 0;
}

_public_ long varlink_object_get_object(VarlinkObject *object, const char *field_name, VarlinkObject **nestedp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->value.kind != VARLINK_VALUE_OBJECT)
                return -VARLINK_ERROR_INVALID_TYPE;

        *nestedp = field->value.object;

        return 0;
}

_public_ long varlink_object_set_null(VarlinkObject *object, const char *field_name) {
        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        return 0;
}

_public_ long varlink_object_set_bool(VarlinkObject *object, const char *field_name, bool b) {
        Field *field;
        long r;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        r = object_add_field(object, field_name, &field);
        if (r < 0)
                return r;

        field->value.kind = VARLINK_VALUE_BOOL;
        field->value.b = b;

        return 0;
}

_public_ long varlink_object_set_int(VarlinkObject *object, const char *field_name, int64_t i) {
        Field *field;
        long r;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        r = object_add_field(object, field_name, &field);
        if (r < 0)
                return r;

        field->value.kind = VARLINK_VALUE_INT;
        field->value.i = i;

        return 0;
}

_public_ long varlink_object_set_float(VarlinkObject *object, const char *field_name, double f) {
        Field *field;
        long r;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        r = object_add_field(object, field_name, &field);
        if (r < 0)
                return r;

        field->value.kind = VARLINK_VALUE_FLOAT;
        field->value.f = f;

        return 0;
}

_public_ long varlink_object_set_string(VarlinkObject *object, const char *field_name, const char *string) {
        Field *field;
        long r;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        r = object_add_field(object, field_name, &field);
        if (r < 0)
                return r;

        field->value.kind = VARLINK_VALUE_STRING;
        field->value.s = strdup(string);
        if (!field->value.s)
                return -VARLINK_ERROR_PANIC;

        return 0;
}

_public_ long varlink_object_set_array(VarlinkObject *object, const char *field_name, VarlinkArray *array) {
        Field *field;
        long r;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        r = object_add_field(object, field_name, &field);
        if (r < 0)
                return r;

        field->value.kind = VARLINK_VALUE_ARRAY;
        field->value.array = varlink_array_ref(array);

        return 0;
}

_public_ long varlink_object_set_object(VarlinkObject *object, const char *field_name, VarlinkObject *nested) {
        Field *field;
        long r;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        object_remove_field(object, field_name);
        r = object_add_field(object, field_name, &field);
        if (r < 0)
                return r;

        field->value.kind = VARLINK_VALUE_OBJECT;
        field->value.object = varlink_object_ref(nested);

        return 0;
}

static long object_write_json(FILE *stream,
                              long indent,
                              bool first) {
        if (!first) {
                if (fprintf(stream, ",") < 0)
                        return -VARLINK_ERROR_PANIC;

                if (indent >= 0)
                        if (fprintf(stream, "\n") < 0)
                                return -VARLINK_ERROR_PANIC;
        }

        for (long l = 0; l < indent; l += 1)
                if (fprintf(stream, "  ") < 0)
                        return -VARLINK_ERROR_PANIC;

        return 0;
}

long varlink_object_write_json(VarlinkObject *object,
                               FILE *stream,
                               long indent,
                               const char *key_pre, const char *key_post,
                               const char *value_pre, const char *value_post) {
        long n_fields;
        _cleanup_(freep) const char **field_names = NULL;
        long r;

        n_fields = varlink_object_get_field_names(object, &field_names);
        if (n_fields < 0)
                return n_fields;

        if (n_fields == 0) {
                if (fprintf(stream, "{}") < 0)
                        return -VARLINK_ERROR_PANIC;
                return 0;
        }

        if (fprintf(stream, "{") < 0)
                return -VARLINK_ERROR_PANIC;

        if (indent >= 0)
                if (fprintf(stream, "\n") < 0)
                        return -VARLINK_ERROR_PANIC;

        for (unsigned long i = 0; i < n_fields; i += 1) {
                Field *field;

                r = object_write_json(stream, indent >= 0 ? indent + 1 : -1, i == 0);
                if (r < 0)
                        return r;

                if (fprintf(stream, "\"%s%s%s\":%s", key_pre, field_names[i], key_post, indent >= 0 ? " ": "") < 0)
                        return -VARLINK_ERROR_PANIC;

                field = avl_tree_find(object->fields, field_names[i]);
                if (!field)
                        return -VARLINK_ERROR_UNKNOWN_FIELD;

                r = varlink_value_write_json(&field->value, stream,
                                             indent >= 0 ? indent + 1 : -1,
                                             key_pre, key_post,
                                             value_pre, value_post);
                if (r < 0)
                        return r;
        }

        if (indent >= 0)
                if (fprintf(stream, "\n") < 0)
                        return -VARLINK_ERROR_PANIC;

        object_write_json(stream, indent, true);
        if (fprintf(stream, "}") < 0)
                return -VARLINK_ERROR_PANIC;

        return 0;
}

long varlink_object_to_pretty_json(VarlinkObject *object,
                                   char **stringp,
                                   long indent,
                                   const char *key_pre, const char *key_post,
                                   const char *value_pre, const char *value_post) {
        _cleanup_(fclosep) FILE *stream = NULL;
        _cleanup_(freep) char *string = NULL;
        size_t size;
        long r;

        if (!key_pre)
                key_pre = "";

        if (!key_post)
                key_post = "";

        if (!value_pre)
                value_pre= "";

        if (!value_post)
                value_post= "";

        stream = open_memstream(&string, &size);

        r = varlink_object_write_json(object, stream, indent, key_pre, key_post, value_pre, value_post);
        if (r < 0)
                return r;

        fclose(stream);
        stream = NULL;

        if (stringp) {
                *stringp = string;
                string = NULL;
        }

        return size;
}

_public_ long varlink_object_to_json(VarlinkObject *object, char **stringp) {
        long ret;
        locale_t old_locale, new_locale;

        old_locale = uselocale((locale_t) 0);

        if (old_locale == (locale_t) 0)
                return -VARLINK_ERROR_PANIC;

        new_locale = duplocale(old_locale);

        if (new_locale == (locale_t) 0)
                return -VARLINK_ERROR_PANIC;

        new_locale = newlocale(LC_NUMERIC_MASK, "C", new_locale);

        if (new_locale == (locale_t) 0)
                return -VARLINK_ERROR_PANIC;

        if (uselocale(new_locale) == (locale_t) 0)
                return -VARLINK_ERROR_PANIC;

        ret = varlink_object_to_pretty_json(object, stringp, -1, NULL, NULL, NULL, NULL);

        uselocale(old_locale);
        freelocale(new_locale);

        return ret;
}
