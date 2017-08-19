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

static bool is_field_char(char c, bool first) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') || (!first && isdigit(c));
}

static bool is_custom_type_char(char c, bool first) {
        return (c >= 'A' && c <= 'Z') || (!first && (c >= 'a' && c <= 'z'));
}

static bool is_interface_char(char c, bool first) {
        return (c >= 'a' && c <= 'z') || c == '.' || c == '-' || isdigit(c);
}

static bool is_member_char(char c, bool first) {
        if (first)
                return c >= 'A' && c <= 'Z';
        else
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isdigit(c);
}

long varlink_type_new(VarlinkType **typep, const char *typestring) {
        _cleanup_(varlink_type_unrefp) VarlinkType *type = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;

        scanner_new_varlink(&scanner, typestring);

        if (!varlink_type_new_from_scanner(&type, scanner) ||
            !scanner_expect_char(scanner, '\0'))
                return -VARLINK_ERROR_INVALID_INTERFACE;

        *typep = type;
        type = NULL;

        return 0;
}

bool varlink_type_new_from_scanner(VarlinkType **typep, Scanner *scanner) {
        _cleanup_(varlink_type_unrefp) VarlinkType *type = NULL;

        switch (scanner_peek(scanner)) {
                case 'b':
                        if (!scanner_expect_keyword(scanner, "bool"))
                                return false;

                        varlink_type_allocate(&type, VARLINK_TYPE_BOOL);
                        break;

                case 'i':
                        if (!scanner_expect_keyword(scanner, "int"))
                                return false;

                        varlink_type_allocate(&type, VARLINK_TYPE_INT);
                        break;

                case 'f':
                        if (!scanner_expect_keyword(scanner, "float"))
                                return false;

                        varlink_type_allocate(&type, VARLINK_TYPE_FLOAT);
                        break;

                case 's':
                        if (!scanner_expect_keyword(scanner, "string"))
                                return false;

                        varlink_type_allocate(&type, VARLINK_TYPE_STRING);
                        break;

                case 'o':
                        if (!scanner_expect_keyword(scanner, "object"))
                                return false;

                        varlink_type_allocate(&type, VARLINK_TYPE_FOREIGN_OBJECT);
                        break;

                case '(': {
                        unsigned long n_fields_allocated = 0;

                        scanner_expect_char(scanner, '(');
                        varlink_type_allocate(&type, VARLINK_TYPE_OBJECT);

                        for (unsigned long i = 0; scanner_peek(scanner) != ')'; i += 1) {
                                _cleanup_(varlink_type_field_freep) VarlinkTypeField *field = NULL;

                                if (i > 0 && !scanner_expect_char(scanner, ','))
                                        return false;

                                field = calloc(1, sizeof(VarlinkTypeField));
                                field->description = scanner_get_last_docstring(scanner);

                                if (!scanner_read_identifier(scanner, is_field_char, &field->name))
                                        return false;

                                if (scanner_expect_char(scanner, ':')) {
                                        if (type->kind == VARLINK_TYPE_ENUM)
                                                return scanner_error(scanner, "No type declaration in enum expected for: %s", field->name);;

                                        if (!varlink_type_new_from_scanner(&field->type, scanner))
                                                return scanner_error(scanner, "Expecting type for: %s", field->name);;
                                } else {
                                        if (i == 0)
                                                type->kind = VARLINK_TYPE_ENUM;

                                        if (type->kind == VARLINK_TYPE_OBJECT)
                                                return scanner_error(scanner, "Missing type declaration for: %s", field->name);;
                                }

                                /* make sure a field with this name doesn't exist yet */
                                if (avl_tree_insert(type->fields_sorted, field->name, field) < 0)
                                        return scanner_error(scanner, "Duplicate field name: %s", field->name);

                                if (type->n_fields == n_fields_allocated) {
                                        n_fields_allocated = MAX(n_fields_allocated * 2, 4);
                                        type->fields = realloc(type->fields, n_fields_allocated * sizeof(VarlinkType *));
                                }

                                type->fields[i] = field;
                                type->n_fields += 1;
                                field = NULL;
                        }

                        if (!scanner_expect_char(scanner, ')'))
                                return false;
                        break;
                }

                case 'A' ... 'Z':
                        varlink_type_allocate(&type, VARLINK_TYPE_ALIAS);

                        if (!scanner_read_identifier(scanner, is_custom_type_char, &type->alias))
                                return false;
                        break;

                default:
                        return scanner_error(scanner, "Expected type");
        }

        if (scanner_peek(scanner) == '[') {
                _cleanup_(varlink_type_unrefp) VarlinkType *array = NULL;

                varlink_type_allocate(&array, VARLINK_TYPE_ARRAY);

                scanner_expect_char(scanner, '[');
                if ((isdigit(scanner_peek(scanner)) && !scanner_read_uint(scanner, &array->fixed_n_elements)) ||
                    !scanner_expect_char(scanner, ']'))
                        return false;

                array->element_type = type;
                type = array;
                array = NULL;
        }

        *typep = type;
        type = NULL;

        return true;
}

static VarlinkTypeField *varlink_type_field_free(VarlinkTypeField *field) {
        if (field->name)
                free(field->name);

        if (field->type)
                varlink_type_unref(field->type);

        free(field->description);
        free(field);

        return NULL;
}

VarlinkType *varlink_type_ref(VarlinkType *type) {
        type->refcount += 1;

        return type;
}

VarlinkType *varlink_type_unref(VarlinkType *type) {
        assert(type->refcount > 0);

        type->refcount -= 1;

        if (type->refcount == 0) {
                switch (type->kind) {
                        case VARLINK_TYPE_BOOL:
                        case VARLINK_TYPE_INT:
                        case VARLINK_TYPE_FLOAT:
                        case VARLINK_TYPE_STRING:
                        case VARLINK_TYPE_FOREIGN_OBJECT:
                                break;

                        case VARLINK_TYPE_ENUM:
                        case VARLINK_TYPE_OBJECT:
                                for (unsigned long i = 0; i < type->n_fields; i += 1)
                                        varlink_type_field_free(type->fields[i]);

                                free(type->fields);
                                avl_tree_free(type->fields_sorted);
                                break;

                        case VARLINK_TYPE_ARRAY:
                                if (type->element_type)
                                        varlink_type_unref(type->element_type);
                                break;

                        case VARLINK_TYPE_ALIAS:
                                free(type->alias);
                                break;
                }

                free(type->typestring);
                free(type);
        }

        return NULL;
}

void varlink_type_unrefp(VarlinkType **typep) {
        if (*typep)
                varlink_type_unref(*typep);
}

static void write_docstring(FILE *stream,
                            long indent,
                            const char *comment_pre, const char *comment_post,
                            const char *description) {
        const char *start = description;

        for (;;) {
                const char *end = strchr(start, '\n');

                if (end) {
                        if (end > start + 1) {
                                for (long l = 0; l < indent; l += 1)
                                        fprintf(stream, "  ");

                                fprintf(stream, "%s# %.*s%s\n", comment_pre, (int)(end - start), start, comment_post);
                        } else {
                                fprintf(stream, "#\n");
                        }
                } else {
                        if (*start) {
                                for (long l = 0; l < indent; l += 1)
                                        fprintf(stream, "  ");

                                fprintf(stream, "%s# %s%s\n", comment_pre, start, comment_post);
                        }

                        break;
                }

                start = end + 1;
        }
}

static void varlink_type_print(VarlinkType *type,
                               FILE *stream,
                               long indent,
                               const char *comment_pre, const char *comment_post,
                               const char *type_pre, const char *type_post) {
        if (!type_pre)
                type_pre = "";

        if (!type_post)
                type_post = "";

        switch (type->kind) {
                case VARLINK_TYPE_BOOL:
                        fprintf(stream, "%sbool%s", type_pre, type_post);
                        break;

                case VARLINK_TYPE_INT:
                        fprintf(stream, "%sint%s", type_pre, type_post);
                        break;

                case VARLINK_TYPE_FLOAT:
                        fprintf(stream, "%sfloat%s", type_pre, type_post);
                        break;

                case VARLINK_TYPE_STRING:
                        fprintf(stream, "%sstring%s", type_pre, type_post);
                        break;

                case VARLINK_TYPE_ENUM:
                case VARLINK_TYPE_OBJECT: {
                        bool docstring = false;

                        fprintf(stream, "(");

                        for (unsigned long i = 0; i < type->n_fields; i += 1) {
                                VarlinkTypeField *field = type->fields[i];

                                if (indent >= 0)
                                        fprintf(stream, "\n");

                                if (field->description) {
                                        if (i > 0 && !docstring)
                                                fprintf(stream, "\n");

                                        write_docstring(stream,
                                                        indent + 1,
                                                        comment_pre, comment_post,
                                                        field->description);

                                        docstring = true;
                                } else
                                        docstring = false;

                                if (indent >= 0)
                                        for (long l = 0; l < indent + 1; l += 1)
                                                fprintf(stream, "  ");

                                fprintf(stream, "%s", field->name);

                                if (type->kind == VARLINK_TYPE_OBJECT) {
                                        fprintf(stream, ": ");

                                        varlink_type_print(field->type,
                                                           stream,
                                                           indent >= 0 ? indent + 1 : -1,
                                                           comment_pre, comment_post,
                                                           type_pre, type_post);
                                }

                                if (i + 1 < type->n_fields) {
                                        fprintf(stream, ", ");

                                        if (field->description)
                                                fprintf(stream, "\n");
                                }
                        }

                        if (indent >= 0) {
                                fprintf(stream, "\n");

                                for (long l = 0; l < indent; l += 1)
                                        fprintf(stream, "  ");
                        }

                        fprintf(stream, ")");
                        break;
                }

                case VARLINK_TYPE_ARRAY:
                        varlink_type_print(type->element_type,
                                           stream,
                                           indent,
                                           comment_pre, comment_post,
                                           type_pre, type_post);
                        fprintf(stream, "[");

                        if (type->fixed_n_elements > 0)
                                fprintf(stream, "%lu", type->fixed_n_elements);

                        fprintf(stream, "]");
                        break;

                case VARLINK_TYPE_FOREIGN_OBJECT:
                        fprintf(stream, "%sobject%s", type_pre, type_post);
                        break;

                case VARLINK_TYPE_ALIAS:
                        fprintf(stream, "%s%s%s", type_pre, type->alias, type_post);
                        break;

                default:
                        abort();
        }
}

const char *varlink_type_get_typestring(VarlinkType *type) {
        FILE *stream = NULL;
        _cleanup_(freep) char *string = NULL;
        unsigned long size;

        if (type->typestring)
                return type->typestring;

        stream = open_memstream(&type->typestring, &size);
        varlink_type_print(type, stream, -1, NULL, NULL, NULL, NULL);

        fclose(stream);

        return type->typestring;
}

static long varlink_type_write_typestring(VarlinkType *type,
                                          FILE *stream,
                                          long indent, long width,
                                          const char *comment_pre, const char *comment_post,
                                          const char *type_pre, const char *type_post) {
        const char *typestring;

        if (!type_pre)
                type_pre = "";

        if (!type_post)
                type_post = "";

        typestring = varlink_type_get_typestring(type);
        if (!typestring)
                return -VARLINK_ERROR_PANIC;

        if (width > 0 && (long)strlen(typestring) > width)
                varlink_type_print(type,
                                   stream,
                                   indent,
                                   comment_pre, comment_post,
                                   type_pre, type_post);
        else
                varlink_type_print(type,
                                   stream,
                                   -1,
                                   comment_pre, comment_post,
                                   type_pre, type_post);

        return 0;
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
        unsigned n_alloced = 0;
        long r;

        varlink_interface_allocate(&interface, NULL);

        interface->description = scanner_get_last_docstring(scanner);

        if (!scanner_expect_keyword(scanner, "interface") ||
            !scanner_read_identifier(scanner, is_interface_char, &interface->name))
                return false;

        if (!varlink_interface_name_valid(interface->name))
                return scanner_error(scanner, "Invalid interface name");

        while (scanner_peek(scanner) != '\0') {
                VarlinkInterfaceMember *member;

                if (n_alloced == interface->n_members) {
                        n_alloced = MAX(2 * n_alloced, 16);
                        interface->members = realloc(interface->members, n_alloced * sizeof(VarlinkInterfaceMember));
                }

                member = &interface->members[interface->n_members];
                interface->n_members += 1;
                memset(member, 0, sizeof(VarlinkInterfaceMember));

                switch (scanner_peek(scanner)) {
                        case 't':
                                member->type = VARLINK_MEMBER_ALIAS;
                                member->description = scanner_get_last_docstring(scanner);

                                if (!scanner_expect_keyword(scanner, "type") ||
                                    !scanner_read_identifier(scanner, is_member_char, &member->name) ||
                                    !varlink_type_new_from_scanner(&member->alias, scanner))
                                        return false;

                                if (member->alias->kind != VARLINK_TYPE_OBJECT)
                                        return scanner_error(scanner, "Type definitions must be objects");
                                break;

                        case 'm':
                                member->type = VARLINK_MEMBER_METHOD;
                                member->method = calloc(1, sizeof(VarlinkMethod));
                                member->description = scanner_get_last_docstring(scanner);

                                if (!scanner_expect_keyword(scanner, "method") ||
                                    !scanner_read_identifier(scanner, is_member_char, &member->name) ||
                                    !varlink_type_new_from_scanner(&member->method->type_in, scanner) ||
                                    !scanner_read_arrow(scanner) ||
                                    !varlink_type_new_from_scanner(&member->method->type_out, scanner))
                                        return false;

                                if (member->method->type_in->kind != VARLINK_TYPE_OBJECT ||
                                    member->method->type_out->kind != VARLINK_TYPE_OBJECT)
                                        return scanner_error(scanner, "Method input and output parameters must be literal objects");
                                break;

                        case 'e':
                                member->type = VARLINK_MEMBER_ERROR;
                                member->description = scanner_get_last_docstring(scanner);

                                if (!scanner_expect_keyword(scanner, "error") ||
                                    !scanner_read_identifier(scanner, is_member_char, &member->name) ||
                                    !varlink_type_new_from_scanner(&member->error, scanner))
                                        return false;

                                if (member->error->kind != VARLINK_TYPE_OBJECT)
                                        return scanner_error(scanner, "Error data must be an object");
                                break;

                        default:
                                return scanner_error(scanner, "Expected 'type', 'method', or 'error'");
                }

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
                           const char *interfacestring,
                           VarlinkParseError **errorp) {
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        _cleanup_(scanner_freep) Scanner *scanner = NULL;

        scanner_new_varlink(&scanner, interfacestring);

        if (!varlink_interface_new_from_scanner(&interface, scanner) ||
            !scanner_expect_char(scanner, '\0')) {
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

long varlink_interface_write_interfacestring(VarlinkInterface *interface,
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
                long remaining = width;

                fprintf(stream, "\n\n");

                if (member->description)
                        write_docstring(stream, indent,
                                        comment_pre, comment_post,
                                        member->description);

                for (long l = 0; l < indent; l += 1)
                        fprintf(stream, "  ");

                remaining -= indent * 2;

                switch (member->type) {
                        case VARLINK_MEMBER_ALIAS:
                                fprintf(stream, "%stype%s %s%s%s ",
                                        keyword_pre, keyword_post,
                                        type_pre,
                                        member->name,
                                        type_post);

                                remaining -= 8;
                                remaining -= strlen(member->name);
                                if (remaining < 0)
                                        remaining = 0;

                                r = varlink_type_write_typestring(member->alias,
                                                                  stream,
                                                                  indent, remaining,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;
                                break;
                        case VARLINK_MEMBER_METHOD:
                                fprintf(stream, "%smethod%s %s%s%s",
                                        keyword_pre, keyword_post,
                                        method_pre, member->name, method_post);

                                remaining -= 2;
                                remaining -= strlen(member->name);
                                if (remaining < 0)
                                        remaining = 0;

                                r = varlink_type_write_typestring(member->method->type_in,
                                                                  stream,
                                                                  indent, remaining,
                                                                  comment_pre, comment_post,
                                                                  type_pre, type_post);
                                if (r < 0)
                                        return r;

                                remaining -= strlen(varlink_type_get_typestring(member->method->type_in));

                                fprintf(stream, " %s->%s ", keyword_pre, keyword_post);

                                remaining -= 4;
                                if (remaining < 0)
                                        remaining = 0;

                                r = varlink_type_write_typestring(member->method->type_out,
                                                                  stream,
                                                                  indent, remaining,
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

                                remaining -= 8;
                                remaining -= strlen(member->name);
                                if (remaining < 0)
                                        remaining = 0;

                                if (member->error) {
                                        r = varlink_type_write_typestring(member->error,
                                                                          stream,
                                                                          indent, remaining,
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

static long field_compare(const void *key, void *value) {
        VarlinkTypeField *field = value;

        return strcmp(key, field->name);
}

void varlink_type_field_freep(VarlinkTypeField **fieldp) {
        if (*fieldp)
                varlink_type_field_free(*fieldp);
}

VarlinkType *varlink_type_field_get_type(VarlinkType *type, const char *name) {
        VarlinkTypeField *field;

        assert(type->kind == VARLINK_TYPE_OBJECT);

        field = avl_tree_find(type->fields_sorted, name);
        if (!field)
                return NULL;

        return field->type;
}

long varlink_type_allocate(VarlinkType **typep,
                           VarlinkTypeKind kind) {
        VarlinkType *type;

        type = calloc(1, sizeof(VarlinkType));
        type->refcount = 1;
        type->kind = kind;

        if (kind == VARLINK_TYPE_OBJECT)
                avl_tree_new(&type->fields_sorted, field_compare, NULL);

        *typep = type;

        return 0;
}

const char *varlink_interface_get_member_description(VarlinkInterface *interface, const char *name) {
        VarlinkInterfaceMember *member;

        member = avl_tree_find(interface->member_tree, name);
        if (!member)
                return NULL;

        return member->description;
}
