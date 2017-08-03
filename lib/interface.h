#pragma once

#include "avltree.h"
#include "error.h"
#include "scanner.h"
#include "varlink.h"

typedef struct VarlinkInterface VarlinkInterface;
typedef struct VarlinkInterfaceMember VarlinkInterfaceMember;
typedef struct VarlinkTypeAlias VarlinkTypeAlias;
typedef struct VarlinkMethod VarlinkMethod;
typedef struct VarlinkError VarlinkError;
typedef struct VarlinkType VarlinkType;

typedef enum {
        VARLINK_TYPE_BOOL = 0,
        VARLINK_TYPE_INT,
        VARLINK_TYPE_FLOAT,
        VARLINK_TYPE_STRING,
        VARLINK_TYPE_ARRAY,
        VARLINK_TYPE_OBJECT,
        VARLINK_TYPE_FOREIGN_OBJECT,
        VARLINK_TYPE_ALIAS
} VarlinkTypeKind;

typedef enum {
        VARLINK_MEMBER_ALIAS,
        VARLINK_MEMBER_METHOD,
        VARLINK_MEMBER_ERROR
} VarlinkMemberType;

typedef struct VarlinkTypeField {
        char *name;
        VarlinkType *type;
} VarlinkTypeField;

struct VarlinkType {
        unsigned long refcount;
        char *typestring;
        VarlinkTypeKind kind;

        VarlinkTypeField **fields;
        unsigned long n_fields;
        AVLTree *fields_sorted;

        VarlinkType *element_type;
        unsigned long fixed_n_elements;

        char *alias;
};

struct VarlinkInterface {
        char *name;
        char *description;

        VarlinkInterfaceMember *members;
        unsigned long n_members;

        AVLTree *member_tree;
};

struct VarlinkInterfaceMember {
        char *name;
        char *description;
        VarlinkMemberType type;
        union {
                VarlinkType *alias;
                VarlinkMethod *method;
                VarlinkType *error;
        };
};

struct VarlinkMethod {
        VarlinkType *type_in;
        VarlinkType *type_out;

        VarlinkMethodServerCallback server_callback;
        void *server_callback_userdata;
};

long varlink_type_new(VarlinkType **typep, const char *typestring);
bool varlink_type_new_from_scanner(VarlinkType **typep, Scanner *scanner);
VarlinkType *varlink_type_ref(VarlinkType *type);
VarlinkType *varlink_type_unref(VarlinkType *type);
void varlink_type_unrefp(VarlinkType **typep);

long varlink_type_allocate(VarlinkType **typep,
                           VarlinkTypeKind kind);
void varlink_type_field_freep(VarlinkTypeField **fieldp);

const char *varlink_type_get_typestring(VarlinkType *type);
VarlinkType *varlink_type_field_get_type(VarlinkType *type, const char *name);

long varlink_interface_new(VarlinkInterface **interfacep,
                           const char *interfacestring,
                           VarlinkParseError **errorp);

VarlinkInterface *varlink_interface_free(VarlinkInterface *interface);
void varlink_interface_freep(VarlinkInterface **interface);
VarlinkMethod *varlink_interface_get_method(VarlinkInterface *interface, const char *name);
VarlinkType *varlink_interface_get_type(VarlinkInterface *interface, const char *name);
long varlink_interface_write_interfacestring(VarlinkInterface *interface,
                                             char **stringp,
                                             long indent, long width,
                                             const char *comment_pre, const char *comment_post,
                                             const char *keyword_pre, const char *keyword_post,
                                             const char *method_pre, const char *method_post,
                                             const char *type_pre, const char *type_post);

long varlink_interface_allocate(VarlinkInterface **interfacep, const char *name);
bool varlink_interface_name_valid(const char *name);
long varlink_interface_parse_qualified_name(const char *qualified_name,
                                            char **interfacep,
                                            char **namep);
const char *varlink_interface_get_member_description(VarlinkInterface *interface, const char *name);
