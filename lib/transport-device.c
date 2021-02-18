// SPDX-License-Identifier: Apache-2.0

#include "transport.h"
#include "util.h"
#include "varlink.h"

#include <fcntl.h>

static int strip_parameters(const char *address, char **devicep) {
        char *parm;
        _cleanup_(freep) char *device = NULL;

        parm = strchr(address, ';');
        if (!parm)
                device = strdup(address);
        else
                device = strndup(address, parm - address);
        if (!device)
                return -VARLINK_ERROR_PANIC;

        *devicep = device;
        device = NULL;
        return 0;
}

int varlink_connect_device(const char *address) {
        _cleanup_(freep) char *device = NULL;
        int fd;
        int r;

        r = strip_parameters(address, &device);
        if (r < 0)
                return r;

        fd = open(device, O_RDWR | O_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        return fd;
}
