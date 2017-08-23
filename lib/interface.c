#include "error.h"
#include "interface.h"
#include "service.h"
#include "util.h"

#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool is_interface_char(char c, bool first) {
        return (c >= 'a' && c <= 'z') || c == '.' || c == '-' || isdigit(c);
}

static bool is_member_char(char c, bool first) {
        if (first)
                return c >= 'A' && c <= 'Z';
        else
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isdigit(c);
}

static void write_docstring(FILE *stream,
                            long indent,
                            const char *comment_pre, const char *comment_post,
                            const char *description) {
        for (const char *start = description; *start;) {
                const char *end = strchrnul(start, '\n');
                int len = end - start;

                for (long l = 0; l < indent; l += 1)
                        fprintf(stream, "  ");

                fprintf(stream, "%s#", comment_pre);
                if (len > 0)
                        fprintf(stream, " %.*s", len, start);
                fprintf(stream, "%s\n", comment_post);

                if (*end != '\n')
                        break;

                start = end + 1;
        }
}

static long varlink_interface_try_resolve(VarlinkInterface *interface,
                                          VarlinkType *type,
                                          const char **first_unknownp) {
        long r = 0;

        switch (type->kind) {
                case VARLINK_TYPE_BOOL:
                case VARLINK_TYPE_INT:
                case VARLINK_TYPE_FLOAT:
                case VARLINK_TYPE_STRING:
                case VARLINK_TYPE_ENUM:
                case VARLINK_TYPE_FOREIGN_OBJECT:
                        break;

                case VARLINK_TYPE_ARRAY:
                        r = varlink_interface_try_resolve(interface, type->element_type, first_unknownp);
                        if (r < 0)
                                return r;
                        break;

                case VARLINK_TYPE_OBJECT:
                        for (unsigned long i = 0; i < type->n_fields; i += 1) {
                                r = varlink_interface_try_resolve(interface, type->fields[i]->type, first_unknownp);
                                if (r < 0)
                                        return r;
                        }
                        break;

                case VARLINK_TYPE_ALIAS:
                        if (varlink_interface_get_type(interface, type->alias) == NULL) {
                                if (first_unknownp)
                                        *first_unknownp = type->alias;
                                return r;
                        }
                        break;
        }

        return 0;
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

bool varlink_interface_name_valid(const char *name) {
        unsigned long len;
        bool has_dot = false;
        bool has_alpha = false;

        if (!name)
                return false;

        len = strlen(name);
        if (len < 3 || len > 255)
                return false;

        if (name[0] == '.' || name[len - 1] == '.')
                return false;

        if (name[0] == '-' || name[len - 1] == '-')
                return false;

        for (unsigned long i = 0; i < len; i += 1) {
                switch (name[i]) {
                        case 'a' ... 'z':
                                has_alpha = true;
                                break;

                        case '0' ... '9':
                                break;

                        case '.':
                                if (name[i - 1] == '.')
                                        return false;

                                if (name[i - 1] == '.')
                                        return false;

                                if (!has_alpha)
                                        return false;

                                has_dot = true;
                                break;

                        case '-':
                                if (name[i - 1] == '.')
                                        return false;

                                break;

                        default:
                                return false;
                }
        }

        if (!has_dot || !has_alpha)
                return false;

        return true;
}

static long member_compare(const void *key, void *value) {
        VarlinkInterfaceMember *member = value;

        return strcmp(key, member->name);
}

long varlink_interface_allocate(VarlinkInterface **interfacep, const char *name) {
        VarlinkInterface *interface;

        interface = calloc(1, sizeof(VarlinkInterface));
        interface->name = name ? strdup(name) : NULL;
        avl_tree_new(&interface->member_tree, member_compare, NULL);

        *interfacep = interface;

        return 0;
}

static bool varlink_interface_new_from_scanner(VarlinkInterface **interfacep, Scanner *scanner) {
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        unsigned n_allocated = 0;
        long r;

        varlink_interface_allocate(&interface, NULL);

        interface->description = scanner_get_last_docstring(scanner);

        if (!scanner_read_keyword(scanner, "interface"))
                return scanner_error(scanner, "'interface' expected");

        if (!scanner_read_identifier(scanner, is_interface_char, &interface->name) ||
            !varlink_interface_name_valid(interface->name))
                return scanner_error(scanner, "Invalid interface name");

        while (scanner_peek(scanner) != '\0') {
                VarlinkInterfaceMember *member;

                if (n_allocated == interface->n_members) {
                        n_allocated = MAX(2 * n_allocated, 16);
                        interface->members = realloc(interface->members, n_allocated * sizeof(VarlinkInterfaceMember));
                }

                member = &interface->members[interface->n_members];
                interface->n_members += 1;
                memset(member, 0, sizeof(VarlinkInterfaceMember));

                if (scanner_read_keyword(scanner, "type")) {
                        member->type = VARLINK_MEMBER_ALIAS;
                        member->description = scanner_get_last_docstring(scanner);

                        if (!scanner_expect_identifier(scanner, is_member_char, &member->name) ||
                            !varlink_type_new_from_scanner(&member->alias, scanner))
                                return false;

                        if (member->alias->kind != VARLINK_TYPE_OBJECT)
                                return scanner_error(scanner, "Type definitions must be objects");

                } else if (scanner_read_keyword(scanner, "method")) {
                        member->type = VARLINK_MEMBER_METHOD;
                        member->method = calloc(1, sizeof(VarlinkMethod));
                        member->description = scanner_get_last_docstring(scanner);

                        if (!scanner_expect_identifier(scanner, is_member_char, &member->name) ||
                            !varlink_type_new_from_scanner(&member->method->type_in, scanner) ||
                            !scanner_expect_operator(scanner, "->") ||
                            !varlink_type_new_from_scanner(&member->method->type_out, scanner))
                                return false;

                        if (member->method->type_in->kind != VARLINK_TYPE_OBJECT ||
                            member->method->type_out->kind != VARLINK_TYPE_OBJECT)
                                return scanner_error(scanner, "Method input and output parameters must be literal objects");

                } else if (scanner_read_keyword(scanner, "error")) {
                        member->type = VARLINK_MEMBER_ERROR;
                        member->description = scanner_get_last_docstring(scanner);

                        if (!scanner_expect_identifier(scanner, is_member_char, &member->name) ||
                            !varlink_type_new_from_scanner(&member->error, scanner))
                                return false;

                        if (member->error->kind != VARLINK_TYPE_OBJECT)
                                return scanner_error(scanner, "Error data must be an object");

                } else
                        return scanner_error(scanner, "'type', 'method', or 'error' expected");

                r = avl_tree_insert(interface->member_tree, member->name, member);
                if (r < 0)
                        return scanner_error(scanner, "Duplicate member: %s", member->name);
        }

        /* check if all referenced types exist */
        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                VarlinkInterfaceMember *member = &interface->members[i];
                const char *first_unknown;

                switch (member->type) {
                        case VARLINK_MEMBER_ALIAS:
                                r = varlink_interface_try_resolve(interface, member->alias, &first_unknown);
                                if (r < 0)
                                        return scanner_error(scanner, "Unkown type: %s", first_unknown);
                                break;
                        case VARLINK_MEMBER_METHOD:
                                r = varlink_interface_try_resolve(interface, member->method->type_in, &first_unknown);
                                if (r < 0)
                                        return scanner_error(scanner, "Unkown type: %s", first_unknown);

                                r = varlink_interface_try_resolve(interface, member->method->type_out, &first_unknown);
                                if (r < 0)
                                        return scanner_error(scanner, "Unkown type: %s", first_unknown);
                                break;
                        case VARLINK_MEMBER_ERROR:
                                if (member->error) {
                                        r = varlink_interface_try_resolve(interface, member->error, &first_unknown);
                                        if (r < 0)
                                                return scanner_error(scanner, "Unkown type: %s", first_unknown);
                                }
                                break;
                }
        }

        *interfacep = interface;
        interface = NULL;

        return true;
}

long varlink_interface_new(VarlinkInterface **interfacep,
                           const char *description,
                           VarlinkParseError **errorp) {
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;

        scanner_new_interface(&scanner, description);

        if (!varlink_interface_new_from_scanner(&interface, scanner) ||
            scanner_peek(scanner) != '\0') {
                scanner_steal_error(scanner, errorp);
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
                                         long indent, long width,
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

        if (interface->description)
                write_docstring(stream,
                                indent,
                                comment_pre, comment_post,
                                interface->description);

        for (long l = 0; l < indent; l += 1)
                fprintf(stream, "  ");

        fprintf(stream, "%sinterface%s %s",
                keyword_pre, keyword_post,
                interface_name);

        for (unsigned long i = 0; i < interface->n_members; i += 1) {
                VarlinkInterfaceMember *member = &interface->members[i];

                fprintf(stream, "\n\n");

                if (member->description)
                        write_docstring(stream, indent,
                                        comment_pre, comment_post,
                                        member->description);

                for (long l = 0; l < indent; l += 1)
                        fprintf(stream, "  ");

                switch (member->type) {
                        case VARLINK_MEMBER_ALIAS:
                                fprintf(stream, "%stype%s %s%s%s ",
                                        keyword_pre, keyword_post,
                                        type_pre,
                                        member->name,
                                        type_post);

                                r = varlink_type_write_typestring(member->alias,
                                                                  stream,
                                                                  indent,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;
                                break;
                        case VARLINK_MEMBER_METHOD:
                                fprintf(stream, "%smethod%s %s%s%s",
                                        keyword_pre, keyword_post,
                                        method_pre, member->name, method_post);

                                r = varlink_type_write_typestring(member->method->type_in,
                                                                  stream,
                                                                  indent,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;

                                fprintf(stream, " %s->%s ", keyword_pre, keyword_post);

                                r = varlink_type_write_typestring(member->method->type_out,
                                                                  stream,
                                                                  indent,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;
                                break;
                        case VARLINK_MEMBER_ERROR:
                                fprintf(stream, "%serror%s %s%s%s ",
                                        keyword_pre, keyword_post,
                                        type_pre,
                                        member->name,
                                        type_post);

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
                fprintf(stream, "  ");
        fprintf(stream, "\n");

        fclose(stream);
        stream = NULL;

        if (stringp) {
                *stringp = string;
                string = NULL;
        }

        return 0;
}

long varlink_interface_parse_qualified_name(const char *qualified_name,
                                            char **interfacep,
                                            char **namep) {
        const char *dot;

        dot = strrchr(qualified_name, '.');
        if (!dot)
                return -VARLINK_ERROR_INVALID_METHOD;

        if (interfacep)
                *interfacep = strndup(qualified_name, dot - qualified_name);

        if (namep)
                *namep = strdup(dot + 1);

        return 0;
}

const char *varlink_interface_get_member_description(VarlinkInterface *interface, const char *name) {
        VarlinkInterfaceMember *member;

        member = avl_tree_find(interface->member_tree, name);
        if (!member)
                return NULL;

        return member->description;
}
