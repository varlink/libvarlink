#include "interface.h"
#include "service.h"
#include "util.h"
#include "varlink.h"

#include <assert.h>
#include <stdarg.h>
#include <string.h>

static void test_basic(void) {
        VarlinkType *type;

        assert(varlink_type_new(&type, "int") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "int") == 0);
        assert(varlink_type_unref(type) == NULL);

        assert(varlink_type_new(&type, "float") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "float") == 0);
        assert(varlink_type_unref(type) == NULL);

        assert(varlink_type_new(&type, "bool") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "bool") == 0);
        assert(varlink_type_unref(type) == NULL);

        assert(varlink_type_new(&type, "string") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "string") == 0);
        assert(varlink_type_unref(type) == NULL);

        assert(varlink_type_new(&type, "int[]") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "int[]") == 0);
        assert(varlink_type_unref(type) == NULL);
}

static void test_object(void) {
        {
                VarlinkType *type;

                assert(varlink_type_new(&type, "(a: int)") == 0);
                assert(varlink_type_unref(type) == NULL);
                assert(varlink_type_new(&type, "(B: int)") == 0);
                assert(varlink_type_unref(type) == NULL);
                assert(varlink_type_new(&type, "(aB: int)") == 0);
                assert(varlink_type_unref(type) == NULL);
                assert(varlink_type_new(&type, "(Ba: int)") == 0);
                assert(varlink_type_unref(type) == NULL);
                assert(varlink_type_new(&type, "(a_b: int)") == 0);
                assert(varlink_type_unref(type) == NULL);
                assert(varlink_type_new(&type, "(a__b: int)") == -VARLINK_ERROR_INVALID_TYPE);
                assert(varlink_type_new(&type, "(_a: int)") == -VARLINK_ERROR_INVALID_TYPE);
                assert(varlink_type_new(&type, "(a_: int)") == -VARLINK_ERROR_INVALID_TYPE);
        }

        {
                VarlinkType *type;

                assert(varlink_type_new(&type, "()") == 0);
                assert(strcmp(varlink_type_get_typestring(type), "()") == 0);
                assert(varlink_type_field_get_type(type, "none") == NULL);
                assert(varlink_type_unref(type) == NULL);
        }

        {
                VarlinkType *type;
                VarlinkType *field_type;

                assert(varlink_type_new(&type, "(one: string, two: int[])") == 0);
                assert(strcmp(varlink_type_get_typestring(type), "(one: string, two: int[])") == 0);

                field_type = varlink_type_field_get_type(type, "one");
                assert(field_type);
                assert(strcmp(varlink_type_get_typestring(field_type), "string") == 0);

                field_type = varlink_type_field_get_type(type, "two");
                assert(field_type);
                assert(strcmp(varlink_type_get_typestring(field_type), "int[]") == 0);

                assert(varlink_type_unref(type) == NULL);
        }

        {
                VarlinkType *type;
                VarlinkType *field_type, *field_type2;

                assert(varlink_type_new(&type, "(info: (one: string, two: int))") == 0);
                assert(strcmp(varlink_type_get_typestring(type), "(info: (one: string, two: int))") == 0);

                field_type = varlink_type_field_get_type(type, "info");
                assert(field_type);
                assert(strcmp(varlink_type_get_typestring(field_type), "(one: string, two: int)") == 0);

                field_type2 = varlink_type_field_get_type(field_type, "one");
                assert(field_type2);
                assert(strcmp(varlink_type_get_typestring(field_type2), "string") == 0);

                field_type = varlink_type_field_get_type(field_type, "two");
                assert(field_type);
                assert(strcmp(varlink_type_get_typestring(field_type), "int") == 0);

                assert(varlink_type_unref(type) == NULL);
        }
        {
                VarlinkType *type;

                assert(varlink_type_new(&type, "(one: string, two: string, one: string)") == -VARLINK_ERROR_INVALID_TYPE);
        }
}

static void test_enum(void) {
        VarlinkType *type;
        VarlinkType *field_type;

        assert(varlink_type_new(&type, "(enumeration: (one, two, three))") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "(enumeration: (one, two, three))") == 0);

        field_type = varlink_type_field_get_type(type, "enumeration");
        assert(field_type);
        assert(strcmp(varlink_type_get_typestring(field_type), "(one, two, three)") == 0);
        assert(varlink_type_unref(type) == NULL);
}

static void test_interface_type_add(void) {
        VarlinkService *service;
        const char *description;
        VarlinkInterface *interface;
        VarlinkType *type;

        description = "interface foo.bar\n"
                      "type One (one: string)\n"
                      "type Two (two: string)\n"
                      "type OneTwo (one: One, two: Two)\n";
        assert(varlink_service_new(&service, "Varlink", "Test Service", "0.1", "http://", "@org.example.foo", -1) == 0);
        assert(varlink_service_add_interface(service, description, NULL) == 0);

        interface = varlink_service_get_interface_by_name(service, "foo.bar");
        assert(interface);

        type = varlink_interface_get_type(interface, "OneTwo");
        assert(type && strcmp(varlink_type_get_typestring(type), "(one: One, two: Two)") == 0);

        assert(varlink_service_free(service) == NULL);
}

static void test_interface_type_lookup(void) {
        VarlinkService *service;
        const char *description;
        VarlinkInterface *interface;
        VarlinkType *type;

        description = "interface foo.bar\n"
                      "type FooBar (foo: int, bar: string)\n";
        assert(varlink_service_new(&service, "Varlink", "Test Service", "0.1", "http://", "@org.example.foo", -1) == 0);
        assert(varlink_service_add_interface(service, description, NULL) == 0);

        interface = varlink_service_get_interface_by_name(service, "foo.bar");
        assert(interface);

        assert(varlink_interface_get_type(interface, "FooBar") != NULL);

        assert(varlink_type_new(&type, "(foo: FooBar)") == 0);
        assert(varlink_type_unref(type) == NULL);

        assert(varlink_service_free(service) == NULL);
}

static void test_type_get_typestring(void) {
        const char *cases[] = {
                "int",
                "float",
                "string[]",
                "string",
                "()",
                "(foo: string, bar: int[], baz: string[])"
        };

        for (unsigned long i = 0; i < ARRAY_SIZE(cases); i += 1) {
                VarlinkType *type;
                const char *str;

                assert(varlink_type_new(&type, cases[i]) == 0);
                str = varlink_type_get_typestring(type);
                assert(str && strcmp(str, cases[i]) == 0);

                assert(varlink_type_unref(type) == NULL);
        }
}

static void test_recursive_types(void) {
        const char *description;
        VarlinkInterface *interface;
        VarlinkType *type, *foobar_type;

        description = "interface foo.bar\n"
                      "type FooBar (foo: int, bar: FooBar)\n";
        assert(varlink_interface_new(&interface, description, NULL) == 0);

        foobar_type = varlink_interface_get_type(interface, "FooBar");
        assert(foobar_type && strcmp(varlink_type_get_typestring(foobar_type), "(foo: int, bar: FooBar)") == 0);

        assert(varlink_type_new(&type, "(foo: FooBar)") == 0);
        assert(strcmp(varlink_type_get_typestring(type), "(foo: FooBar)") == 0);
        assert(varlink_type_unref(type) == NULL);

        assert(varlink_interface_free(interface) == NULL);
}

static void test_nonexisting(void) {
        const char *cases[] = {
                "foo.bar { type Foo (foo: Bar) }",
                "foo.bar { Foo(foo: Bar) -> () }",
                "foo.bar { Foo() -> (foo: Bar) }",
                "foo.bar { type Foo\nFoo() -> (foo: Bar) }"
        };
        VarlinkInterface *interface;

        for (unsigned long i = 0; i < ARRAY_SIZE(cases); i += 1)
                assert(varlink_interface_new(&interface, cases[i], NULL) == -VARLINK_ERROR_INVALID_INTERFACE);
}

int main(void) {
        test_basic();
        test_object();
        test_enum();
        test_interface_type_add();
        test_interface_type_lookup();
        test_type_get_typestring();
        test_recursive_types();
        test_nonexisting();

        return EXIT_SUCCESS;
}
