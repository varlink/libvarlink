// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include "varlink.h"
#include "util.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <glob.h>
#include <errno.h>

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
        char *json;
        long len;

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

        len = varlink_object_to_json(s, &json);
        assert(len >= 0);

        assert(varlink_object_unref(s) == NULL);
        assert(varlink_object_new_from_json(&s, json) == 0);
        assert(varlink_object_get_float(s, "float", &f) == 0);
        assert(fabs(f - 42.2) < 1e-100);
        free(json);

        assert(varlink_object_unref(s) == NULL);

        /* json escape sequences */
        assert(varlink_object_new_from_json(&s, "{ \"foo\": \"\\n\\t\\/\\b\\f\\u00e4\" }") == 0);
        assert(varlink_object_get_string(s, "foo", &string) == 0);
        assert(strcmp(string, "\n\t/\b\fä") == 0);
        assert(varlink_object_unref(s) == NULL);
}

static int read_file(FILE *file, char **contentsp) {
        _cleanup_ (fclosep) FILE *stream = NULL;
        _cleanup_ (freep) char *contents = NULL;
        size_t n_input;

        stream = open_memstream(&contents, &n_input);
        if (!stream)
                return -errno;

        fputs("{ \"test\" : ", stream);

        for (;;) {
                char buffer[8192];
                size_t n;

                n = fread(buffer, 1, sizeof(buffer), file);
                if (n == 0) {
                        if (ferror(file))
                                return -errno;

                        break;
                }

                if (fwrite(buffer, 1, n, stream) != n)
                        return -errno;
        }

        fputs("}", stream);
        fclose(stream);
        stream = NULL;

        *contentsp = contents;
        contents = NULL;

        return 0;
}

static int test_json_test_suite(char *base_dir) {
        _cleanup_(globfree) glob_t globbuf;
        _cleanup_(freep) char *realsrc = NULL;

        int j;
        int r = 0;

        // All n_*.json and i_*.json
        assert(asprintf(&realsrc, "%s/[ni]_*.json", base_dir) >= 0);
        assert(glob(realsrc, 0, NULL, &globbuf) == 0);
        for (j = 0; j < globbuf.gl_pathc; j++) {
                _cleanup_(freep) char *input = NULL;
                _cleanup_(fclosep) FILE *file = NULL;
                VarlinkObject *s;

                file = fopen(globbuf.gl_pathv[j], "r");
                assert(file != NULL);
                assert(read_file(file, &input) == 0);
                fprintf(stderr, "%s ... ", globbuf.gl_pathv[j]);
                if (varlink_object_new_from_json(&s, input) != 0) {
                        fprintf(stderr, "[OK]\n");
                } else {
                        fprintf(stderr, "[FAILED]\n");
                        assert(varlink_object_unref(s) == NULL);
                        r++;
                }
        }

        free(realsrc);
        realsrc = NULL;

        globfree(&globbuf);
        memset(&globbuf, 0, sizeof(globbuf));

        // All y_*.json
        assert(asprintf(&realsrc, "%s/y_*.json", base_dir) >= 0);
        assert(glob(realsrc, 0, NULL, &globbuf) == 0);

        for (j = 0; j < globbuf.gl_pathc; j++) {
                _cleanup_(freep) char *input = NULL;
                _cleanup_(fclosep) FILE *file = NULL;
                VarlinkObject *s;

                file = fopen(globbuf.gl_pathv[j], "r");
                assert(file != NULL);
                assert(read_file(file, &input) == 0);
                fprintf(stderr, "%s ... ", globbuf.gl_pathv[j]);
                if (varlink_object_new_from_json(&s, input) == 0) {
                        fprintf(stderr, "[OK]\n");
                        assert(varlink_object_unref(s) == NULL);
                } else {
                        fprintf(stderr, "[FAILED]\n");
                        r++;
                }
        }

        return r;
}

int main(int argc, char **argv) {

        assert(argc != 1);

        // Uses `,` as the radix character
        assert(setlocale(LC_NUMERIC, "de_DE.UTF-8") != 0);

        test_api();

        assert(test_json_test_suite(argv[1]) == 0);

        test_json();

        return EXIT_SUCCESS;
}
