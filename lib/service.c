#include "interface.h"
#include "service.h"
#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "org.varlink.service.varlink.inc.c"

VarlinkService *varlink_service_free(VarlinkService *service) {
        if (service->properties)
                varlink_object_unref(service->properties);

        avl_tree_free(service->interfaces);
        free(service);

        return NULL;
}

void varlink_service_freep(VarlinkService **servicep) {
        if (*servicep)
                varlink_service_free(*servicep);
}

static long interface_compare(const void *key, void *value) {
        VarlinkInterface *interface = value;

        return strcmp(key, interface->name);
}

long varlink_service_new(VarlinkService **servicep, VarlinkObject *properties) {
        _cleanup_(varlink_service_freep) VarlinkService *service = NULL;
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        long r;

        service = calloc(1, sizeof(VarlinkService));
        avl_tree_new(&service->interfaces, interface_compare, (AVLFreeFunc)varlink_interface_free);

        r = varlink_interface_new(&interface, org_varlink_service_varlink, NULL);
        if (r < 0)
                return r;

        r = varlink_service_add_interface(service, interface);
        if (r < 0)
                return r;

        if (properties)
                service->properties = varlink_object_ref(properties);

        interface = NULL;

        *servicep = service;
        service = NULL;

        return 0;
}

long varlink_service_add_interface(VarlinkService *service, VarlinkInterface *interface) {
        long r;

        r = avl_tree_insert(service->interfaces, interface->name, interface);
        if (r < 0)
                return -VARLINK_ERROR_DUPLICATE_INTERFACE;

        return 0;
}

VarlinkInterface *varlink_service_get_interface_by_name(VarlinkService *service, const char *name) {
        return avl_tree_find(service->interfaces, name);
}
