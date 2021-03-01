// SPDX-License-Identifier: Apache-2.0

#include "array.h"
#include "util.h"

#include <string.h>

struct VarlinkArray {
        unsigned long refcount;
        VarlinkValueKind element_kind;

        VarlinkValue *elements;
        unsigned long n_elements;
        unsigned long n_allocated_elements;

        bool writable;
};

static long array_append(VarlinkArray *array, VarlinkValue **valuep) {
        if (array->n_elements == array->n_allocated_elements) {
                array->n_allocated_elements = MAX(array->n_allocated_elements * 2, 16);
                array->elements = realloc(array->elements, array->n_allocated_elements * sizeof(VarlinkValue));
                if (!array->elements)
                        return -VARLINK_ERROR_PANIC;
        }

        *valuep = &array->elements[array->n_elements];
        array->n_elements += 1;

        return 0;
}

VarlinkValueKind varlink_array_get_element_kind(VarlinkArray *array) {
        return array->element_kind;
}

_public_ long varlink_array_new(VarlinkArray **arrayp) {
        _cleanup_(varlink_array_unrefp) VarlinkArray *array = NULL;

        array = calloc(1, sizeof(VarlinkArray));
        if (!array)
                return -VARLINK_ERROR_PANIC;

        array->refcount = 1;
        array->writable = true;

        *arrayp = array;
        array = NULL;

        return 0;
}

long varlink_array_new_from_scanner(VarlinkArray **arrayp, Scanner *scanner, locale_t locale, unsigned long depth_cnt) {
        _cleanup_(varlink_array_unrefp) VarlinkArray *array = NULL;
        bool first = true;
        long r;

        r = varlink_array_new(&array);
        if (r < 0)
                return r;

        if (scanner_expect_operator(scanner, "[") < 0)
                return -VARLINK_ERROR_INVALID_JSON;

        while (scanner_peek(scanner) != ']') {
                VarlinkValue *value;

                if (!first) {
                        if (scanner_expect_operator(scanner, ",") < 0)
                                return -VARLINK_ERROR_INVALID_JSON;
                }

                r = array_append(array, &value);
                if (r < 0)
                        return r;

                if (!varlink_value_read_from_scanner(value, scanner, locale, depth_cnt))
                        return -VARLINK_ERROR_INVALID_JSON;

                /* Accept `null` value for any element kind */
                if (value->kind != VARLINK_VALUE_NULL) {
                        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                                array->element_kind = value->kind;
                        else if (array->element_kind != value->kind)
                                return -VARLINK_ERROR_INVALID_JSON;
                }

                first = false;
        }

        if (scanner_expect_operator(scanner, "]") < 0)
                return -VARLINK_ERROR_INVALID_JSON;

        *arrayp = array;
        array = NULL;

        return 0;
}

_public_ VarlinkArray *varlink_array_ref(VarlinkArray *array) {
        array->refcount += 1;
        return array;
}

_public_ VarlinkArray *varlink_array_unref(VarlinkArray *array) {
        array->refcount -= 1;

        if (array->refcount == 0) {
                for (unsigned long i = 0; i < array->n_elements; i += 1)
                        varlink_value_clear(&array->elements[i]);

                free(array->elements);
                free(array);
        }

        return NULL;
}

_public_ void varlink_array_unrefp(VarlinkArray **arrayp) {
        if (*arrayp)
                varlink_array_unref(*arrayp);
}

_public_ unsigned long varlink_array_get_n_elements(VarlinkArray *array) {
        return array->n_elements;
}

_public_ long varlink_array_get_bool(VarlinkArray *array, unsigned long index, bool *bp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->elements[index].kind != VARLINK_VALUE_BOOL)
                return -VARLINK_ERROR_INVALID_TYPE;

        *bp = array->elements[index].b;

        return 0;
}

_public_ long varlink_array_get_int(VarlinkArray *array, unsigned long index, int64_t *ip) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->elements[index].kind != VARLINK_VALUE_INT)
                return -VARLINK_ERROR_INVALID_TYPE;

        *ip = array->elements[index].i;

        return 0;
}

_public_ long varlink_array_get_float(VarlinkArray *array, unsigned long index, double *fp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->elements[index].kind != VARLINK_VALUE_FLOAT)
                return -VARLINK_ERROR_INVALID_TYPE;

        *fp = array->elements[index].f;

        return 0;
}

_public_ long varlink_array_get_string(VarlinkArray *array, unsigned long index, const char **stringp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->elements[index].kind != VARLINK_VALUE_STRING)
                return -VARLINK_ERROR_INVALID_TYPE;

        *stringp = array->elements[index].s;

        return 0;
}

_public_ long varlink_array_get_array(VarlinkArray *array, unsigned long index, VarlinkArray **elementp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->elements[index].kind != VARLINK_VALUE_ARRAY)
                return -VARLINK_ERROR_INVALID_TYPE;

        *elementp = array->elements[index].array;

        return 0;
}

_public_ long varlink_array_get_object(VarlinkArray *array, unsigned long index, VarlinkObject **objectp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->elements[index].kind != VARLINK_VALUE_OBJECT)
                return -VARLINK_ERROR_INVALID_TYPE;

        *objectp = array->elements[index].object;

        return 0;
}

long varlink_array_get_value(VarlinkArray *array, unsigned long index, VarlinkValue **valuep) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        *valuep = &array->elements[index];

        return 0;
}

_public_ long varlink_array_append_null(VarlinkArray *array) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_NULL;

        return 0;
}

_public_ long varlink_array_append_bool(VarlinkArray *array, bool b) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                array->element_kind = VARLINK_VALUE_BOOL;
        else if (array->element_kind != VARLINK_VALUE_BOOL)
                return -VARLINK_ERROR_INVALID_TYPE;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_BOOL;
        v->b = b;

        return 0;
}

_public_ long varlink_array_append_int(VarlinkArray *array, int64_t i) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                array->element_kind = VARLINK_VALUE_INT;
        else if (array->element_kind != VARLINK_VALUE_INT)
                return -VARLINK_ERROR_INVALID_TYPE;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_INT;
        v->i = i;

        return 0;
}

_public_ long varlink_array_append_float(VarlinkArray *array, double f) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                array->element_kind = VARLINK_VALUE_FLOAT;
        else if (array->element_kind != VARLINK_VALUE_FLOAT)
                return -VARLINK_ERROR_INVALID_TYPE;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_FLOAT;
        v->f = f;

        return 0;
}

_public_ long varlink_array_append_string(VarlinkArray *array, const char *string) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                array->element_kind = VARLINK_VALUE_STRING;
        else if (array->element_kind != VARLINK_VALUE_STRING)
                return -VARLINK_ERROR_INVALID_TYPE;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_STRING;
        v->s = strdup(string);
        if (!v->s)
                return -VARLINK_ERROR_PANIC;

        return 0;
}

_public_ long varlink_array_append_array(VarlinkArray *array, VarlinkArray *element) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                array->element_kind = VARLINK_VALUE_ARRAY;
        else if (array->element_kind != VARLINK_VALUE_ARRAY)
                return -VARLINK_ERROR_INVALID_TYPE;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_ARRAY;
        v->array = varlink_array_ref(element);

        return 0;
}

_public_ long varlink_array_append_object(VarlinkArray *array, VarlinkObject *object) {
        VarlinkValue *v;
        long r;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->element_kind == VARLINK_VALUE_UNDEFINED)
                array->element_kind = VARLINK_VALUE_OBJECT;
        else if (array->element_kind != VARLINK_VALUE_OBJECT)
                return -VARLINK_ERROR_INVALID_TYPE;

        r = array_append(array, &v);
        if (r < 0)
                return r;

        v->kind = VARLINK_VALUE_OBJECT;
        v->object = varlink_object_ref(object);

        return 0;
}

long varlink_array_write_json(VarlinkArray *array,
                              FILE *stream,
                              long indent,
                              const char *key_pre, const char *key_post,
                              const char *value_pre, const char *value_post) {
        long r;

        if (array->n_elements == 0) {
                if (fprintf(stream, "[]") < 0)
                        return -VARLINK_ERROR_PANIC;

                return 0;
        }

        if (fprintf(stream, "[") < 0)
                return -VARLINK_ERROR_PANIC;

        if (indent >= 0)
                if (fprintf(stream, "\n") < 0)
                        return -VARLINK_ERROR_PANIC;

        for (unsigned long i = 0; i < array->n_elements; i += 1) {
                if (i > 0) {
                        if (fprintf(stream, ",") < 0)
                                return -VARLINK_ERROR_PANIC;

                        if (indent >= 0)
                                if (fprintf(stream, "\n") < 0)
                                        return -VARLINK_ERROR_PANIC;
                }

                if (indent >= 0)
                        if (fprintf(stream, "%*s", (int)(indent + 1) * 2, " ") < 0)
                                return -VARLINK_ERROR_PANIC;

                r = varlink_value_write_json(&array->elements[i], stream,
                                             indent >= 0 ? indent + 1 : -1,
                                             key_pre, key_post,
                                             value_pre, value_post);
                if (r < 0)
                        return r;
        }

        if (indent >= 0)
                if (fprintf(stream, "\n%*s", (int)indent * 2, " ") < 0)
                        return -VARLINK_ERROR_PANIC;

        if (fprintf(stream, "]") < 0)
                return -VARLINK_ERROR_PANIC;

        return 0;
}
