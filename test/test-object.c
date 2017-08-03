#include "varlink.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void test_api(void) {
        VarlinkObject *s;
        bool b;
        int64_t i;
        double f;
        const char *string;
        VarlinkArray *array;
        VarlinkObject *nested;

        assert(varlink_object_new(&s) == 0);

        /* non-existing fields */
        assert(varlink_object_get_bool(s, "foo", &b) == -VARLINK_ERROR_UNKNOWN_FIELD);
        assert(varlink_object_get_int(s, "foo", &i) == -VARLINK_ERROR_UNKNOWN_FIELD);
        assert(varlink_object_get_float(s, "foo", &f) == -VARLINK_ERROR_UNKNOWN_FIELD);
        assert(varlink_object_get_string(s, "foo", &string) == -VARLINK_ERROR_UNKNOWN_FIELD);
        assert(varlink_object_get_array(s, "foo", &array) == -VARLINK_ERROR_UNKNOWN_FIELD);
        assert(varlink_object_get_object(s, "foo", &nested) == -VARLINK_ERROR_UNKNOWN_FIELD);

        /* empty field value */
        assert(varlink_object_get_bool(s, "", &b) == -VARLINK_ERROR_UNKNOWN_FIELD);

        /* set/get */
        assert(varlink_object_set_bool(s, "b", true) == 0);
        assert(varlink_object_get_bool(s, "b", &b) == 0);
        assert(b == true);
        assert(varlink_object_set_int(s, "i", 42) == 0);
        assert(varlink_object_get_int(s, "i", &i) == 0);
        assert(i == 42);
        assert(varlink_object_set_float(s, "f", 42) == 0);
        assert(varlink_object_get_float(s, "f", &f) == 0);
        assert(fabs(f - 42) < 1e-100);
        assert(varlink_object_set_string(s, "s", "foo") == 0);
        assert(varlink_object_get_string(s, "s", &string) == 0);
        assert(strcmp(string, "foo") == 0);

        assert(varlink_object_unref(s) == NULL);
}

static void test_json(void) {
        VarlinkObject *s;
        bool b;
        int64_t i;
        double f;
        const char *string;
        VarlinkArray *array;
        VarlinkObject *nested;

        /* some invalid json */
        assert(varlink_object_new_from_json(&s, "") == -VARLINK_ERROR_INVALID_JSON);
        assert(varlink_object_new_from_json(&s, "{") == -VARLINK_ERROR_INVALID_JSON);
        assert(varlink_object_new_from_json(&s, "}") == -VARLINK_ERROR_INVALID_JSON);
        assert(varlink_object_new_from_json(&s, "{ \"foo:") == -VARLINK_ERROR_INVALID_JSON);

        /* empty */
        assert(varlink_object_new_from_json(&s, "{}") == 0);
        assert(varlink_object_get_field_names(s, NULL) == 0);
        assert(varlink_object_unref(s) == NULL);

        /*
         * Setting a key to null should be treated the same way as if
         * the key was missing.
         */
        assert(varlink_object_new_from_json(&s, "{ \"foo\": null }") == 0);
        assert(varlink_object_get_field_names(s, NULL) == 0);
        assert(varlink_object_get_string(s, "foo", &string) == -VARLINK_ERROR_UNKNOWN_FIELD);
        assert(varlink_object_unref(s) == NULL);

        /* some values */
        assert(varlink_object_new_from_json(&s, "{"
                                                "  \"bool\": true,"
                                                "  \"int\": 42,"
                                                "  \"float\": 42.2,"
                                                "  \"string\": \"foo\","
                                                "  \"array\": [],"
                                                "  \"object\": {}"
                                                "}") == 0);
        assert(varlink_object_get_field_names(s, NULL) == 6);
        assert(varlink_object_get_bool(s, "bool", &b) == 0);
        assert(b == true);
        assert(varlink_object_get_int(s, "int", &i) == 0);
        assert(i == 42);
        assert(varlink_object_get_float(s, "float", &f) == 0);
        assert(fabs(f - 42.2) < 1e-100);
        assert(varlink_object_get_string(s, "string", &string) == 0);
        assert(strcmp(string, "foo") == 0);
        assert(varlink_object_get_array(s, "array", &array) == 0);
        assert(array);
        assert(varlink_object_get_object(s, "object", &nested) == 0);
        assert(nested);
        assert(varlink_object_unref(s) == NULL);

        /* json escape sequences */
        assert(varlink_object_new_from_json(&s, "{ \"foo\": \"\\n\\t\\/\\b\\f\\u00e4\" }") == 0);
        assert(varlink_object_get_string(s, "foo", &string) == 0);
        assert(strcmp(string, "\n\t/\b\fä") == 0);
        assert(varlink_object_unref(s) == NULL);
}

int main(void) {
        test_api();
        test_json();

        return EXIT_SUCCESS;
}
