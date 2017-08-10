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

VarlinkService *varlink_service_free(VarlinkService *service) {
        free(service->name);
        free(service->version);

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

long varlink_service_new(VarlinkService **servicep, const char *name, const char *version) {
        _cleanup_(varlink_service_freep) VarlinkService *service = NULL;

        service = calloc(1, sizeof(VarlinkService));
        service->name = strdup(name);
        service->version = strdup(version);
        avl_tree_new(&service->interfaces, interface_compare, (AVLFreeFunc)varlink_interface_free);

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
