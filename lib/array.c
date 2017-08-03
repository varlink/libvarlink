
#include "array.h"

#include "util.h"

#include <string.h>

struct VarlinkArray {
        unsigned long refcount;
        VarlinkTypeKind element_kind;

        VarlinkValue *elements;
        unsigned long n_elements;
        unsigned long n_alloced_elements;

        bool writable;
};

static VarlinkValue *array_append(VarlinkArray *array) {
        VarlinkValue *v;

        if (array->n_elements == array->n_alloced_elements) {
                array->n_alloced_elements = MAX(array->n_alloced_elements * 2, 32);
                array->elements = realloc(array->elements, array->n_alloced_elements * sizeof(VarlinkValue));
        }

        v = &array->elements[array->n_elements];
        array->n_elements += 1;

        return v;
}

VarlinkTypeKind varlink_array_get_element_kind(VarlinkArray *array) {
        return array->element_kind;
}

_public_ long varlink_array_new(VarlinkArray **arrayp) {
        _cleanup_(varlink_array_unrefp) VarlinkArray *array = NULL;

        array = calloc(1, sizeof(VarlinkArray));
        array->refcount = 1;
        array->writable = true;

        *arrayp = array;
        array = NULL;

        return 0;
}

long varlink_array_new_from_scanner(VarlinkArray **arrayp, Scanner *scanner) {
        _cleanup_(varlink_array_unrefp) VarlinkArray *array = NULL;
        bool first = true;

        if (varlink_array_new(&array) != 0)
                return false;

        if (!scanner_expect_char(scanner, '['))
                return false;

        while (scanner_peek(scanner) != ']') {
                VarlinkTypeKind kind;
                VarlinkValue *value;

                if (!first && !scanner_expect_char(scanner, ','))
                        return false;

                value = array_append(array);
                if (!varlink_value_read_from_scanner(&kind, value, scanner))
                        return false;

                if (first)
                        array->element_kind = kind;
                else if (kind != array->element_kind)
                        return false;

                first = false;
        }

        if (!scanner_expect_char(scanner, ']'))
                return false;

        *arrayp = array;
        array = NULL;

        return true;
}

_public_ VarlinkArray *varlink_array_ref(VarlinkArray *array) {
        array->refcount += 1;
        return array;
}

_public_ VarlinkArray *varlink_array_unref(VarlinkArray *array) {
        array->refcount -= 1;

        if (array->refcount == 0) {
                for (unsigned long i = 0; i < array->n_elements; i += 1)
                        varlink_value_clear(array->element_kind, &array->elements[i]);

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

        if (array->element_kind != VARLINK_TYPE_BOOL)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *bp = array->elements[index].b;

        return 0;
}

_public_ long varlink_array_get_int(VarlinkArray *array, unsigned long index, int64_t *ip) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->element_kind != VARLINK_TYPE_INT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *ip = array->elements[index].i;

        return 0;
}

_public_ long varlink_array_get_float(VarlinkArray *array, unsigned long index, double *fp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->element_kind != VARLINK_TYPE_FLOAT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *fp = array->elements[index].f;

        return 0;
}

_public_ long varlink_array_get_string(VarlinkArray *array, unsigned long index, const char **stringp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->element_kind != VARLINK_TYPE_STRING)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *stringp = array->elements[index].s;

        return 0;
}

_public_ long varlink_array_get_array(VarlinkArray *array, unsigned long index, VarlinkArray **elementp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->element_kind != VARLINK_TYPE_ARRAY)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *elementp = array->elements[index].array;

        return 0;
}

_public_ long varlink_array_get_object(VarlinkArray *array, unsigned long index, VarlinkObject **objectp) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        if (array->element_kind != VARLINK_TYPE_OBJECT &&
            array->element_kind != VARLINK_TYPE_FOREIGN_OBJECT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        *objectp = array->elements[index].object;

        return 0;
}

long varlink_array_get_value(VarlinkArray *array, unsigned long index, VarlinkValue **valuep) {
        if (index >= array->n_elements)
                return -VARLINK_ERROR_INVALID_INDEX;

        *valuep = &array->elements[index];

        return 0;
}

_public_ long varlink_array_append_bool(VarlinkArray *array, bool b) {
        VarlinkValue *v;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->n_elements == 0)
                array->element_kind = VARLINK_TYPE_BOOL;
        else if (array->element_kind != VARLINK_TYPE_BOOL)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        v = array_append(array);
        v->b = b;

        return 0;
}

_public_ long varlink_array_append_int(VarlinkArray *array, int64_t i) {
        VarlinkValue *v;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->n_elements == 0)
                array->element_kind = VARLINK_TYPE_INT;
        else if (array->element_kind != VARLINK_TYPE_INT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        v = array_append(array);
        v->i = i;

        return 0;
}

_public_ long varlink_array_append_float(VarlinkArray *array, double f) {
        VarlinkValue *v;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->n_elements == 0)
                array->element_kind = VARLINK_TYPE_FLOAT;
        else if (array->element_kind != VARLINK_TYPE_FLOAT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        v = array_append(array);
        v->f = f;

        return 0;
}

_public_ long varlink_array_append_string(VarlinkArray *array, const char *string) {
        VarlinkValue *v;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->n_elements == 0)
                array->element_kind = VARLINK_TYPE_STRING;
        else if (array->element_kind != VARLINK_TYPE_STRING)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        v = array_append(array);
        v->s = strdup(string);

        return 0;
}

_public_ long varlink_array_append_array(VarlinkArray *array, VarlinkArray *element) {
        VarlinkValue *v;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->n_elements == 0)
                array->element_kind = VARLINK_TYPE_ARRAY;
        else if (array->element_kind != VARLINK_TYPE_ARRAY)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        v = array_append(array);
        v->array = varlink_array_ref(element);

        return 0;
}

_public_ long varlink_array_append_object(VarlinkArray *array, VarlinkObject *object) {
        VarlinkValue *v;

        if (!array->writable)
                return -VARLINK_ERROR_READ_ONLY;

        if (array->n_elements == 0)
                array->element_kind = VARLINK_TYPE_OBJECT;
        else if (array->element_kind != VARLINK_TYPE_OBJECT)
                return -VARLINK_ERROR_TYPE_MISMATCH;

        v = array_append(array);
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
                fprintf(stream, "[]");
                return 0;
        }

        fprintf(stream, "[");

        if (indent >= 0)
                fprintf(stream, "\n");

        for (unsigned long i = 0; i < array->n_elements; i += 1) {
                if (i > 0) {
                        fprintf(stream, ",");
                        if (indent >= 0)
                                fprintf(stream, "\n");
                }

                if (indent >= 0)
                        fprintf(stream, "%*s", (int)(indent + 1) * 2, " ");

                r = varlink_value_write_json(array->element_kind, &array->elements[i], stream,
                                             indent >= 0 ? indent + 1 : -1,
                                             key_pre, key_post,
                                             value_pre, value_post);
                if (r < 0)
                        return r;
        }

        if (indent >= 0)
                fprintf(stream, "\n%*s", (int)indent * 2, " ");

        fprintf(stream, "]");

        return 0;
}
