#pragma once

#include <avltree.h>
#include <varlink.h>

typedef struct VarlinkService {
        VarlinkObject *properties;
        AVLTree *interfaces;
} VarlinkService;

long varlink_service_new(VarlinkService **servicep, VarlinkObject *properties);
VarlinkService *varlink_service_free(VarlinkService *service);
void varlink_service_freep(VarlinkService **servicepp);
VarlinkInterface *varlink_service_get_interface_by_name(VarlinkService *service, const char *name);
long varlink_service_add_interface(VarlinkService *service, VarlinkInterface *interface);
