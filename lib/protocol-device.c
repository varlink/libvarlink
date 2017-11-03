#include "protocol.h"
#include "util.h"
#include "varlink.h"

#include <fcntl.h>

int varlink_connect_device(const char *device) {
        int fd;

        fd = open(device, O_RDWR | O_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        return fd;
}
