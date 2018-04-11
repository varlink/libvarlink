#include "error.h"
#include "interface.h"
#include "service.h"
#include "scanner.h"
#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long write_docstring(FILE *stream,
                            long indent,
                            const char *comment_pre, const char *comment_post,
                            const char *description) {
        for (const char *start = description; *start;) {
                const char *end = strchrnul(start, '\n');
                int len = end - start;

                for (long l = 0; l < indent; l += 1)
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

        return 0;
}

static bool varlink_interface_try_resolve(VarlinkInterface *interface,
                                          VarlinkType *type,
                                          const char **first_unknownp) {
        switch (type->kind) {
                case VARLINK_TYPE_BOOL:
                case VARLINK_TYPE_INT:
                case VARLINK_TYPE_FLOAT:
                case VARLINK_TYPE_STRING:
                case VARLINK_TYPE_MAYBE:
                case VARLINK_TYPE_ENUM:
                case VARLINK_TYPE_FOREIGN_OBJECT:
                        break;

                case VARLINK_TYPE_ARRAY:
                case VARLINK_TYPE_MAP:
                        if (!varlink_interface_try_resolve(interface, type->element_type, first_unknownp))
                                return false;

                        break;

                case VARLINK_TYPE_OBJECT:
                        for (unsigned long i = 0; i < type->n_fields; i += 1)
                                if (!varlink_interface_try_resolve(interface, type->fields[i]->type, first_unknownp))
                                        return false;
                        break;

                case VARLINK_TYPE_ALIAS:
                        if (!varlink_interface_get_type(interface, type->alias)) {
                                if (first_unknownp)
                                        *first_unknownp = type->alias;

                                return false;
                        }
                        break;
        }

        return true;
}

static VarlinkMethod *varlink_method_free(VarlinkMethod *method) {
        if (method->type_in)
                varlink_type_unref(method->type_in);

        if (method->type_out)
                varlink_type_unref(method->type_out);

        free(method);

        return NULL;
}

VarlinkInterface *varlink_interface_free(VarlinkInterface *interface) {
        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                VarlinkInterfaceMember *member = &interface->members[i];

                free(member->name);
                free(member->description);

                switch (member->type) {
                        case VARLINK_MEMBER_ALIAS:
                                if (member->alias)
                                        varlink_type_unref(member->alias);
                                break;
                        case VARLINK_MEMBER_METHOD:
                                if (member->method)
                                        varlink_method_free(member->method);
                                break;
                        case VARLINK_MEMBER_ERROR:
                                if (member->error)
                                        varlink_type_unref(member->error);
                                break;
                }
        }

        avl_tree_free(interface->member_tree);
        free(interface->members);
        free(interface->name);
        free(interface->description);
        free(interface);

        return NULL;
}

void varlink_interface_freep(VarlinkInterface **interfacep) {
        if (*interfacep)
                varlink_interface_free(*interfacep);
}

static long member_compare(const void *key, void *value) {
        VarlinkInterfaceMember *member = value;

        return strcmp(key, member->name);
}

static long varlink_interface_new_from_scanner(VarlinkInterface **interfacep, Scanner *scanner) {
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        unsigned n_allocated = 0;
        long r;

        interface = calloc(1, sizeof(VarlinkInterface));
        if (!interface)
                return -VARLINK_ERROR_PANIC;

        r = avl_tree_new(&interface->member_tree, member_compare, NULL);
        if (r < 0)
                return r;

        r = scanner_get_last_docstring(scanner, &interface->description);
        if (r < 0)
                return r;

        if (!scanner_read_keyword(scanner, "interface")) {
                scanner_error(scanner, SCANNER_ERROR_INTERFACE_KEYWORD_EXPECTED);
                return -VARLINK_ERROR_INVALID_INTERFACE;
        }

        r = scanner_expect_interface_name(scanner, &interface->name);
        if (r < 0) {
                scanner_error(scanner, SCANNER_ERROR_INTERFACE_NAME_INVALID);
                return r;
        }

        while (scanner_peek(scanner) != '\0') {
                VarlinkInterfaceMember *member;

                if (n_allocated == interface->n_members) {
                        n_allocated = MAX(2 * n_allocated, 16);
                        interface->members = realloc(interface->members, n_allocated * sizeof(VarlinkInterfaceMember));
                        if (!interface->members)
                                return -VARLINK_ERROR_PANIC;
                }

                member = &interface->members[interface->n_members];
                interface->n_members += 1;
                memset(member, 0, sizeof(VarlinkInterfaceMember));

                if (scanner_read_keyword(scanner, "type")) {
                        member->type = VARLINK_MEMBER_ALIAS;
                        r = scanner_get_last_docstring(scanner, &member->description);
                        if (r < 0)
                                return r;

                        r = scanner_expect_member_name(scanner, &member->name);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                                return r;
                        }

                        r = varlink_type_new_from_scanner(&member->alias, scanner);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                                return r;
                        }

                        if (member->alias->kind != VARLINK_TYPE_OBJECT &&
                            member->alias->kind != VARLINK_TYPE_ENUM) {
                                scanner_error(scanner, SCANNER_ERROR_OBJECT_EXPECTED);
                                return -VARLINK_ERROR_INVALID_INTERFACE;
                        }

                } else if (scanner_read_keyword(scanner, "method")) {
                        member->type = VARLINK_MEMBER_METHOD;
                        member->method = calloc(1, sizeof(VarlinkMethod));
                        r = scanner_get_last_docstring(scanner, &member->description);
                        if (r < 0)
                                return r;

                        r = scanner_expect_member_name(scanner, &member->name);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                                return r;
                        }

                        r = varlink_type_new_from_scanner(&member->method->type_in, scanner);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                                return r;
                        }

                        if (scanner_expect_operator(scanner, "->") < 0) {
                                scanner_error(scanner, SCANNER_ERROR_OPERATOR_EXPECTED);
                                return -VARLINK_ERROR_INVALID_INTERFACE;
                        }

                        r = varlink_type_new_from_scanner(&member->method->type_out, scanner);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_TYPE_EXPECTED);
                                return -VARLINK_ERROR_INVALID_INTERFACE;
                        }

                        if (member->method->type_in->kind != VARLINK_TYPE_OBJECT ||
                            member->method->type_out->kind != VARLINK_TYPE_OBJECT) {
                                scanner_error(scanner, SCANNER_ERROR_OBJECT_EXPECTED);
                                return -VARLINK_ERROR_INVALID_INTERFACE;
                        }

                } else if (scanner_read_keyword(scanner, "error")) {
                        member->type = VARLINK_MEMBER_ERROR;
                        r = scanner_get_last_docstring(scanner, &member->description);
                        if (r < 0)
                                return r;

                        r = scanner_expect_member_name(scanner, &member->name);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                                return r;
                        }

                        r = varlink_type_new_from_scanner(&member->error, scanner);
                        if (r < 0) {
                                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                                return r;
                        }

                        if (member->error->kind != VARLINK_TYPE_OBJECT) {
                                scanner_error(scanner, SCANNER_ERROR_OBJECT_EXPECTED);
                                return -VARLINK_ERROR_INVALID_INTERFACE;
                        }

                } else {
                        scanner_error(scanner, SCANNER_ERROR_KEYWORD_EXPECTED);
                        return -VARLINK_ERROR_INVALID_INTERFACE;
                }

                r = avl_tree_insert(interface->member_tree, member->name, member);
                if (r < 0) {
                        switch (r) {
                                case 0:
                                        break;

                                case -AVL_ERROR_KEY_EXISTS:
                                        scanner_error(scanner, SCANNER_ERROR_DUPLICATE_MEMBER_NAME);
                                        return -VARLINK_ERROR_INVALID_INTERFACE;

                                default:
                                        return -VARLINK_ERROR_PANIC;
                        }
                }
        }

        /* check if all referenced types exist */
        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                VarlinkInterfaceMember *member = &interface->members[i];
                const char *first_unknown;

                switch (member->type) {
                        case VARLINK_MEMBER_ALIAS:
                                if (!varlink_interface_try_resolve(interface, member->alias, &first_unknown)) {
                                        scanner_error(scanner, SCANNER_ERROR_UNKNOWN_TYPE);
                                        return -VARLINK_ERROR_INVALID_INTERFACE;
                                }
                                break;

                        case VARLINK_MEMBER_METHOD:
                                if (!varlink_interface_try_resolve(interface, member->method->type_in, &first_unknown)) {
                                        scanner_error(scanner, SCANNER_ERROR_UNKNOWN_TYPE);
                                        return -VARLINK_ERROR_INVALID_INTERFACE;
                                }

                                if (!varlink_interface_try_resolve(interface, member->method->type_out, &first_unknown)) {
                                        scanner_error(scanner, SCANNER_ERROR_UNKNOWN_TYPE);
                                        return -VARLINK_ERROR_INVALID_INTERFACE;
                                }
                                break;

                        case VARLINK_MEMBER_ERROR:
                                if (member->error) {
                                        if (!varlink_interface_try_resolve(interface, member->error, &first_unknown)) {
                                                scanner_error(scanner, SCANNER_ERROR_UNKNOWN_TYPE);
                                                return -VARLINK_ERROR_INVALID_INTERFACE;
                                        }
                                }
                                break;
                }
        }

        *interfacep = interface;
        interface = NULL;

        return 0;
}

long varlink_interface_new(VarlinkInterface **interfacep,
                           const char *description,
                           Scanner **scannerp) {
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;
        long r;

        r = scanner_new(&scanner, description, true);
        if (r < 0)
                return r;

        if (varlink_interface_new_from_scanner(&interface, scanner) < 0 ||
            scanner_peek(scanner) != '\0') {
                if (scannerp) {
                        *scannerp = scanner;
                        scanner = NULL;
                }

                return -VARLINK_ERROR_INVALID_INTERFACE;
        }

        *interfacep = interface;
        interface = NULL;

        return 0;
}

VarlinkType *varlink_interface_get_type(VarlinkInterface *interface, const char *name) {
        VarlinkInterfaceMember *member;

        member = avl_tree_find(interface->member_tree, name);
        if (!member || member->type != VARLINK_MEMBER_ALIAS)
                return NULL;

        return member->alias;
}

VarlinkMethod *varlink_interface_get_method(VarlinkInterface *interface, const char *name) {
        VarlinkInterfaceMember *member;

        member = avl_tree_find(interface->member_tree, name);
        if (!member || member->type != VARLINK_MEMBER_METHOD)
                return NULL;

        return member->method;
}

long varlink_interface_write_description(VarlinkInterface *interface,
                                         char **stringp,
                                         long indent,
                                         const char *comment_pre, const char *comment_post,
                                         const char *keyword_pre, const char *keyword_post,
                                         const char *method_pre, const char *method_post,
                                         const char *type_pre, const char *type_post) {
        _cleanup_(fclosep) FILE *stream = NULL;
        _cleanup_(freep) char *string = NULL;
        unsigned long size;
        const char *interface_name;
        long r;

        if (!comment_pre)
                comment_pre = "";

        if (!comment_post)
                comment_post = "";

        if (!keyword_pre)
                keyword_pre = "";

        if (!keyword_post)
                keyword_post = "";

        if (!method_pre)
                method_pre = "";

        if (!method_post)
                method_post = "";

        if (!type_pre)
                type_pre= "";

        if (!type_post)
                type_post= "";

        stream = open_memstream(&string, &size);

        interface_name = interface->name;

        if (interface->description) {
                r = write_docstring(stream,
                                    indent,
                                    comment_pre, comment_post,
                                    interface->description);
                if (r < 0)
                        return r;
        }

        for (long l = 0; l < indent; l += 1)
                if (fprintf(stream, "  ") < 0)
                        return -VARLINK_ERROR_PANIC;

        if (fprintf(stream, "%sinterface%s %s",
                    keyword_pre, keyword_post,
                    interface_name) < 0)
                return -VARLINK_ERROR_PANIC;

        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                VarlinkInterfaceMember *member = &interface->members[i];

                if (fprintf(stream, "\n\n") < 0)
                        return -VARLINK_ERROR_PANIC;

                if (member->description) {
                        r = write_docstring(stream, indent,
                                            comment_pre, comment_post,
                                            member->description);
                        if (r < 0)
                                return r;
                }

                for (long l = 0; l < indent; l += 1)
                        if (fprintf(stream, "  ") < 0)
                                return -VARLINK_ERROR_PANIC;

                switch (member->type) {
                        case VARLINK_MEMBER_ALIAS:
                                if (fprintf(stream, "%stype%s %s%s%s ",
                                            keyword_pre, keyword_post,
                                            type_pre,
                                            member->name,
                                            type_post) < 0)
                                        return -VARLINK_ERROR_PANIC;

                                r = varlink_type_write_typestring(member->alias,
                                                                  stream,
                                                                  indent,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;
                                break;
                        case VARLINK_MEMBER_METHOD:
                                if (fprintf(stream, "%smethod%s %s%s%s",
                                            keyword_pre, keyword_post,
                                            method_pre, member->name, method_post) < 0)
                                        return -VARLINK_ERROR_PANIC;

                                r = varlink_type_write_typestring(member->method->type_in,
                                                                  stream,
                                                                  indent,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;

                                if (fprintf(stream, " %s->%s ", keyword_pre, keyword_post) < 0)
                                        return -VARLINK_ERROR_PANIC;

                                r = varlink_type_write_typestring(member->method->type_out,
                                                                  stream,
                                                                  indent,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;
                                break;
                        case VARLINK_MEMBER_ERROR:
                                if (fprintf(stream, "%serror%s %s%s%s ",
                                            keyword_pre, keyword_post,
                                            type_pre,
                                            member->name,
                                            type_post) < 0)
                                        return -VARLINK_ERROR_PANIC;

                                if (member->error) {
                                        r = varlink_type_write_typestring(member->error,
                                                                          stream,
                                                                          indent,
                                                                          comment_pre, comment_post,
                                                                          type_pre, type_post);
                                        if (r < 0)
                                                return r;
                                }
                                break;
                }
        }

        for (long l = 0; l < indent; l += 1)
                if (fprintf(stream, "  ") < 0)
                        return -VARLINK_ERROR_PANIC;

        if (fprintf(stream, "\n") < 0)
                return -VARLINK_ERROR_PANIC;

        fclose(stream);
        stream = NULL;

        if (stringp) {
                *stringp = string;
                string = NULL;
        }

        return 0;
}

const char *varlink_interface_get_member_description(VarlinkInterface *interface, const char *name) {
        VarlinkInterfaceMember *member;

        member = avl_tree_find(interface->member_tree, name);
        if (!member)
                return NULL;

        return member->description;
}
