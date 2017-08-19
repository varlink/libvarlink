#include "interface.h"
#include "util.h"

#include <assert.h>
#include <string.h>

static void test_invalid(void) {
        const char *cases[] = {
                "interface com.example.test\ntypeFoo()",

                /* errors must have types */
                "interface com.example.test\nerror Foo\n",

                /* methods must have object literals as in and out parameters */
                "interface com.example.test\nmethod Foo() -> string"
                "interface com.example.test\nmethod Foo int -> ()"
                "interface com.example.test\ntype Bar ()\nmethod Foo Bar -> ()"

                /* types must be objects */
                "interface com.example.test\ntype Foo string"

                /* this will valid once enums work - allow setting them
                 * as types, too */
                "interface com.example.test\ntype Foo (one, two, three)"
        };

        for (unsigned long c = 0; c < ARRAY_SIZE(cases); c += 1) {
                const char *string = cases[c];
                _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
                _cleanup_(varlink_parse_error_freep) VarlinkParseError *error = NULL;

                assert(varlink_interface_new(&interface, string, &error) == -VARLINK_ERROR_INVALID_INTERFACE);
                assert(error);
        }
}

static void test_name(void) {
        const char *valid[] = {
                "a.b",
                "a.b.c",
                "a1.b1.c1",
                "a--1.b--1.c--1",
                "a.21.c"
        };

        const char *invalid[] = {
                /* needs a dot */
                "ab",

                /* only one dot, but not at the start or end */
                ".a.b.c",
                "a.b.c.",
                "a..b.c",

                /* the first element needs an alpha character */
                "21.b.c",

                /* no dashes at the beginning and end of an element */
                "-a.b.c",

                "a.b.c-",
                "a.b-.c-",
                "a.-.c",

                /* illegal character */
                "a.*.c",
                "a.?"
        };

        for (unsigned long i = 0; i < ARRAY_SIZE(valid); i += 1) {
                _cleanup_(freep) char *string = NULL;
                VarlinkInterface *interface;

                asprintf(&string, "interface %s", valid[i]);
                assert(varlink_interface_new(&interface, string, NULL) == 0);
                assert(varlink_interface_free(interface) == NULL);
        }

        for (unsigned long i = 0; i < ARRAY_SIZE(invalid); i += 1) {
                _cleanup_(freep) char *string = NULL;
                VarlinkInterface *interface;
                VarlinkParseError *error = NULL;

                asprintf(&string, "interface %s", invalid[i]);
                assert(varlink_interface_new(&interface, string, &error) == -VARLINK_ERROR_INVALID_INTERFACE);
                assert(error);
                assert(varlink_parse_error_free(error) == NULL);
        }
}

static void test_method_name(void) {
        const char *valid[] = {
                "A",
                "AFoo",
                "A565465",
        };

        const char *invalid[] = {
                "a",  /* lowercase */
                "5a", /* number */
                "_a", /* illegal character */
        };

        for (unsigned long i = 0; i < ARRAY_SIZE(valid); i += 1) {
                _cleanup_(freep) char *string = NULL;
                VarlinkInterface *interface;

                asprintf(&string, "interface a.b\nmethod %s() -> ()", valid[i]);
                assert(varlink_interface_new(&interface, string, NULL) == 0);
                assert(varlink_interface_free(interface) == NULL);
        }

        for (unsigned long i = 0; i < ARRAY_SIZE(invalid); i += 1) {
                _cleanup_(freep) char *string = NULL;
                VarlinkInterface *interface;
                VarlinkParseError *error = NULL;

                asprintf(&string, "interface a.b\nmethod %s() -> ()", invalid[i]);
                assert(varlink_interface_new(&interface, string, &error) == -VARLINK_ERROR_INVALID_INTERFACE);
                assert(error);
                assert(varlink_parse_error_free(error) == NULL);
        }

        /* duplicate */
        {
                const char *string = "interface a.b\n"
                                     "method A() -> ()\n"
                                     "method A() -> ()\n";
                VarlinkInterface *interface;
                VarlinkParseError *error = NULL;

                assert(varlink_interface_new(&interface, string, &error) == -VARLINK_ERROR_INVALID_INTERFACE);
                assert(error);
                assert(varlink_parse_error_free(error) == NULL);
        }
}

static void test_docstrings(void) {
        {
                const char *string = "# A comment\n"
                                     "\n"
                                     "# Description\n"
                                     "# Second line\n"
                                     "interface com.example.foo\n"
                                     "\n"
                                     "# A type\n"
                                     "type A (one: int)\n"
                                     "# A method\n"
                                     "method Foo () -> ()\n";

                VarlinkInterface *interface = NULL;
                VarlinkParseError *error = NULL;

                assert(varlink_interface_new(&interface, string, &error) == 0);
                assert(error == NULL);
                assert(strcmp(interface->description, "Description\nSecond line\n") == 0);
                assert(strcmp(varlink_interface_get_member_description(interface, "A"), "A type\n") == 0);
                assert(strcmp(varlink_interface_get_member_description(interface, "Foo"), "A method\n") == 0);
                assert(varlink_interface_free(interface) == NULL);
        }

        {
                const char *string = "# A comment\n"
                                     "\n"
                                     "interface com.example.foo # trailing doesn't count\n"
                                     "type A (one: int)\n";

                VarlinkInterface *interface = NULL;
                VarlinkParseError *error = NULL;

                assert(varlink_interface_new(&interface, string, &error) == 0);
                assert(error == NULL);
                assert(interface->description == NULL);
                assert(varlink_interface_get_type(interface, "A") != NULL);
                assert(varlink_interface_free(interface) == NULL);
        }
}

/*
 * Round trip a sample interface through the parser and writer to ensure
 * canonical output and that member order is preserved.
 */
static void test_writer(void) {
        const char *cases[] = {
                "interface com.example.test\n",

                "# A test interface.\n"
                "#\n"
                "# It makes testing easy.\n"
                "interface com.example.test\n"
                "\n"
                "# Foo\n"
                "type Foo (one: string, two: bool)\n"
                "\n"
                "method Bar() -> (status: int)\n"
                "\n"
                "# Baz\n"
                "# Baz is described in more detail.\n"
                "type Baz (baz: bool)\n"
        };

        for (unsigned long c = 0; c < ARRAY_SIZE(cases); c += 1) {
                const char *string = cases[c];
                _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
                _cleanup_(varlink_parse_error_freep) VarlinkParseError *error = NULL;
                _cleanup_(freep) char *output = NULL;

                if (varlink_interface_new(&interface, string, &error) < 0) {
                        fprintf(stderr, "case %lu: %lu:%lu: %s\n", c, error->line_nr, error->pos_nr, error->message);
                        assert(false);
                }

                assert(varlink_interface_write_description(interface, &output, 0, 72,
                                                           NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == 0);

                if (strcmp(output, string) != 0) {
                        for (unsigned long i = 0; ; i += 1) {
                                if (string[i] != output[i]) {
                                        fprintf(stderr, "string differs from output at byte %lu (...%s)", i, &output[i]);
                                        break;
                                }
                        }

                        assert(false);
                }
        }
}

int main(void) {
        test_invalid();
        test_method_name();
        test_name();
        test_docstrings();
        test_writer();

        return 0;
}
