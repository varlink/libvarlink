#include "object.h"

#include "array.h"
#include "avltree.h"
#include "scanner.h"
#include "util.h"

#include <inttypes.h>
#include <string.h>

typedef struct Field Field;

struct VarlinkObject {
        unsigned long refcount;
        AVLTree *fields;
        bool writable;
};

struct Field {
        char *name;
        VarlinkTypeKind kind;
        VarlinkValue value;
};

static long field_compare(const void *key, void *value) {
        Field *field = value;

        return strcmp(key, field->name);
}

static void field_free(void *ptr) {
        Field *field = ptr;

        free(field->name);
        varlink_value_clear(field->kind, &field->value);
        free(field);
}

static Field *object_replace(VarlinkObject *object, const char *name) {
        Field *field;

        avl_tree_remove(object->fields, name);

        field = calloc(1, sizeof(Field));
        field->name = strdup(name);

        avl_tree_insert(object->fields, field->name, field);

        return field;
}

_public_ long varlink_object_new(VarlinkObject **objectp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *object = NULL;

        object = calloc(1, sizeof(VarlinkObject));
        object->refcount = 1;
        avl_tree_new(&object->fields, field_compare, field_free);
        object->writable = true;

        *objectp = object;
        object = NULL;

        return 0;
}

bool varlink_object_new_from_scanner(VarlinkObject **objectp, Scanner *scanner) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *object = NULL;
        bool first = true;

        if (!scanner_expect_operator(scanner, "{"))
                return false;

        if (varlink_object_new(&object) < 0)
                return false;

        while (scanner_peek(scanner) != '}') {
                _cleanup_(freep) char *name = NULL;

                if (!first && !scanner_expect_operator(scanner, ","))
                        return false;

                if (!scanner_read_string(scanner, &name) ||
                    !scanner_expect_operator(scanner, ":"))
                        return false;

                /* treat `null` the same as non-existent keys */
                if (!scanner_read_keyword(scanner, "null")) {
                        Field *field;

                        field = object_replace(object, name);
                        if (!varlink_value_read_from_scanner(&field->kind, &field->value, scanner))
                                return false;
                }

                first = false;
        }

        if (!scanner_expect_operator(scanner, "}"))
                return false;

        *objectp = object;
        object = NULL;

        return true;
}

_public_ long varlink_object_new_from_json(VarlinkObject **objectp, const char *json) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *object = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;

        scanner_new_json(&scanner, json);

        if (!varlink_object_new_from_scanner(&object, scanner) ||
            scanner_peek(scanner) != '\0')
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

_public_ unsigned long varlink_object_get_field_names(VarlinkObject *object, const char ***namesp) {
        _cleanup_(freep) const char **names = NULL;
        unsigned long n_fields;

        n_fields = avl_tree_get_n_elements(object->fields);

        if (namesp) {
                AVLTreeNode *node;
                unsigned long i = 0;

                names = calloc(n_fields + 1, sizeof(const char *));

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

        if (field->kind != VARLINK_TYPE_BOOL)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *bp = field->value.b;

        return 0;
}

_public_ long varlink_object_get_int(VarlinkObject *object, const char *field_name, int64_t *ip) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->kind != VARLINK_TYPE_INT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *ip = field->value.i;

        return 0;
}

_public_ long varlink_object_get_float(VarlinkObject *object, const char *field_name, double *fp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->kind == VARLINK_TYPE_INT)
                *fp = field->value.i;
        else if (field->kind == VARLINK_TYPE_FLOAT)
                *fp = field->value.f;
        else
                return -VARLINK_ERROR_TYPE_MISMATCH;

        return 0;
}

_public_ long varlink_object_get_string(VarlinkObject *object, const char *field_name, const char **stringp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->kind != VARLINK_TYPE_STRING)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *stringp = field->value.s;

        return 0;
}

_public_ long varlink_object_get_array(VarlinkObject *object, const char *field_name, VarlinkArray **arrayp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->kind != VARLINK_TYPE_ARRAY)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *arrayp = field->value.array;

        return 0;
}

_public_ long varlink_object_get_object(VarlinkObject *object, const char *field_name, VarlinkObject **nestedp) {
        Field *field;

        field = avl_tree_find(object->fields, field_name);
        if (!field)
                return -VARLINK_ERROR_UNKNOWN_FIELD;

        if (field->kind != VARLINK_TYPE_OBJECT &&
            field->kind != VARLINK_TYPE_FOREIGN_OBJECT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *nestedp = field->value.object;

        return 0;
}

_public_ long varlink_object_set_bool(VarlinkObject *object, const char *field_name, bool b) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_BOOL;
        field->value.b = b;

        return 0;
}

_public_ long varlink_object_set_int(VarlinkObject *object, const char *field_name, int64_t i) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_INT;
        field->value.i = i;

        return 0;
}

_public_ long varlink_object_set_float(VarlinkObject *object, const char *field_name, double f) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_FLOAT;
        field->value.f = f;

        return 0;
}

_public_ long varlink_object_set_string(VarlinkObject *object, const char *field_name, const char *string) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_STRING;
        field->value.s = strdup(string);

        return 0;
}

_public_ long varlink_object_set_array(VarlinkObject *object, const char *field_name, VarlinkArray *array) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_ARRAY;
        field->value.array = varlink_array_ref(array);

        return 0;
}

_public_ long varlink_object_set_object(VarlinkObject *object, const char *field_name, VarlinkObject *nested) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_OBJECT;
        field->value.object = varlink_object_ref(nested);

        return 0;
}

long varlink_object_set_empty_object(VarlinkObject *object, const char *field_name) {
        Field *field;

        if (!object->writable)
                return -VARLINK_ERROR_READ_ONLY;

        field = object_replace(object, field_name);
        field->kind = VARLINK_TYPE_OBJECT;
        varlink_object_new(&field->value.object);

        return 0;
}

static void object_write_json(FILE *stream,
                              long indent,
                              bool first) {
        if (!first) {
                fprintf(stream, ",");
                if (indent >= 0)
                        fprintf(stream, "\n");
        }

        for (long l = 0; l < indent; l += 1)
                fprintf(stream, "  ");
}

long varlink_object_write_json(VarlinkObject *object,
                               FILE *stream,
                               long indent,
                               const char *key_pre, const char *key_post,
                               const char *value_pre, const char *value_post) {
        unsigned long n_fields;
        _cleanup_(freep) const char **field_names;
        long r;

        n_fields = varlink_object_get_field_names(object, &field_names);

        if (n_fields == 0) {
                fprintf(stream, "{}");
                return 0;
        }

        fprintf(stream, "{");
        if (indent >= 0)
                fprintf(stream, "\n");

        for (unsigned long i = 0; i < n_fields; i += 1) {
                Field *field;

                object_write_json(stream, indent >= 0 ? indent + 1 : -1, i == 0);
                fprintf(stream, "\"%s%s%s\":%s", key_pre, field_names[i], key_post, indent >= 0 ? " ": "");

                field = avl_tree_find(object->fields, field_names[i]);
                if (!field)
                        return -VARLINK_ERROR_UNKNOWN_FIELD;

                r = varlink_value_write_json(field->kind, &field->value, stream,
                                             indent >= 0 ? indent + 1 : -1,
                                             key_pre, key_post,
                                             value_pre, value_post);
                if (r < 0)
                        return r;
        }

        if (indent >= 0)
                fprintf(stream, "\n");

        object_write_json(stream, indent, true);
        fprintf(stream, "}");

        return 0;
}

long varlink_object_to_pretty_json(VarlinkObject *object,
                                   char **stringp,
                                   long indent,
                                   const char *key_pre, const char *key_post,
                                   const char *value_pre, const char *value_post) {
        _cleanup_(fclosep) FILE *stream = NULL;
        _cleanup_(freep) char *string = NULL;
        unsigned long size;
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
        return varlink_object_to_pretty_json(object, stringp, -1, NULL, NULL, NULL, NULL);
}
