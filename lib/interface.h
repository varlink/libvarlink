#pragma once

#include "avltree.h"
#include "error.h"
#include "scanner.h"
#include "type.h"
#include "varlink.h"

typedef struct VarlinkInterface VarlinkInterface;
typedef struct VarlinkInterfaceMember VarlinkInterfaceMember;
typedef struct VarlinkTypeAlias VarlinkTypeAlias;
typedef struct VarlinkMethod VarlinkMethod;
typedef struct VarlinkError VarlinkError;

typedef enum {
        VARLINK_MEMBER_ALIAS,
        VARLINK_MEMBER_METHOD,
        VARLINK_MEMBER_ERROR
} VarlinkMemberType;

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

        VarlinkMethodCallback callback;
        void *callback_userdata;
};

long varlink_interface_new(VarlinkInterface **interfacep,
                           const char *description,
                           Scanner **scannerp);

VarlinkInterface *varlink_interface_free(VarlinkInterface *interface);
void varlink_interface_freep(VarlinkInterface **interface);
VarlinkMethod *varlink_interface_get_method(VarlinkInterface *interface, const char *name);
VarlinkType *varlink_interface_get_type(VarlinkInterface *interface, const char *name);
long varlink_interface_write_description(VarlinkInterface *interface,
                                         char **stringp,
                                         long indent,
                                         const char *comment_pre, const char *comment_post,
                                         const char *keyword_pre, const char *keyword_post,
                                         const char *method_pre, const char *method_post,
                                         const char *type_pre, const char *type_post);

long varlink_interface_allocate(VarlinkInterface **interfacep, const char *name);
const char *varlink_interface_get_member_description(VarlinkInterface *interface, const char *name);
