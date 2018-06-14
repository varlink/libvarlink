#include "array.h"
#include "util.h"

#include <assert.h>
#include <string.h>

static void test_api(void) {
        VarlinkArray *array;
        bool b;
        int64_t i;
        double f;
        const char *string;
        VarlinkArray *nested;
        VarlinkObject *s;

        assert(varlink_array_new(&array) == 0);
        assert(varlink_array_get_n_elements(array) == 0);

        assert(varlink_array_get_bool(array, 0, &b) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_array_get_int(array, 0, &i) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_array_get_float(array, 0, &f) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_array_get_string(array, 0, &string) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_array_get_array(array, 0, &nested) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_array_get_object(array, 0, &s) == -VARLINK_ERROR_INVALID_INDEX);

        assert(varlink_array_append_bool(array, true) == 0);
        assert(varlink_array_append_int(array, 42) == -VARLINK_ERROR_INVALID_TYPE);
        assert(varlink_array_append_float(array, 42) == -VARLINK_ERROR_INVALID_TYPE);
        assert(varlink_array_append_string(array, "foo") == -VARLINK_ERROR_INVALID_TYPE);

        assert(varlink_array_unref(array) == NULL);
}

static void test_int(void) {
        VarlinkArray *array;
        int64_t i;

        assert(varlink_array_new(&array) == 0);
        assert(varlink_array_get_n_elements(array) == 0);

        assert(varlink_array_append_int(array, 1) == 0);
        assert(varlink_array_append_int(array, 2) == 0);
        assert(varlink_array_append_int(array, 3) == 0);

        assert(varlink_array_get_n_elements(array) == 3);
        assert(varlink_array_get_int(array, 0, &i) == 0);
        assert(i == 1);
        assert(varlink_array_get_int(array, 1, &i) == 0);
        assert(i == 2);
        assert(varlink_array_get_int(array, 2, &i) == 0);
        assert(i == 3);
        assert(varlink_array_get_int(array, 3, &i) == -VARLINK_ERROR_INVALID_INDEX);

        assert(varlink_array_unref(array) == NULL);
}

static void test_string(void) {
        VarlinkArray *array;
        const char *str;

        assert(varlink_array_new(&array) == 0);
        assert(varlink_array_get_n_elements(array) == 0);

        assert(varlink_array_append_string(array, "one") == 0);
        assert(varlink_array_append_string(array, "two") == 0);
        assert(varlink_array_append_string(array, "three") == 0);

        assert(varlink_array_get_n_elements(array) == 3);
        assert(varlink_array_get_string(array, 0, &str) == 0);
        assert(strcmp(str, "one") == 0);
        assert(varlink_array_get_string(array, 1, &str) == 0);
        assert(strcmp(str, "two") == 0);
        assert(varlink_array_get_string(array, 2, &str) == 0);
        assert(strcmp(str, "three") == 0);
        assert(varlink_array_get_string(array, 3, &str) == -VARLINK_ERROR_INVALID_INDEX);

        assert(varlink_array_unref(array) == NULL);
}

static void test_null(void) {
        VarlinkArray *array;
        const char *str;

        assert(varlink_array_new(&array) == 0);
        assert(varlink_array_get_n_elements(array) == 0);

        assert(varlink_array_append_null(array) == 0);
        assert(varlink_array_append_string(array, "a") == 0);
        assert(varlink_array_append_null(array) == 0);
        assert(varlink_array_append_string(array, "b") == 0);
        assert(varlink_array_append_null(array) == 0);

        assert(varlink_array_get_n_elements(array) == 5);
        assert(varlink_array_get_string(array, 0, &str) == -VARLINK_ERROR_INVALID_TYPE);
        assert(varlink_array_get_string(array, 1, &str) == 0);
        assert(strcmp(str, "a") == 0);
        assert(varlink_array_get_string(array, 2, &str) == -VARLINK_ERROR_INVALID_TYPE);
        assert(varlink_array_get_string(array, 3, &str) == 0);
        assert(strcmp(str, "b") == 0);
        assert(varlink_array_get_string(array, 4, &str) == -VARLINK_ERROR_INVALID_TYPE);
        assert(varlink_array_get_string(array, 5, &str) == -VARLINK_ERROR_INVALID_INDEX);

        assert(varlink_array_unref(array) == NULL);
}

int main(void) {
        test_api();
        test_int();
        test_string();
        test_null();

        return EXIT_SUCCESS;
}
