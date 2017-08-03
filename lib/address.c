#include "address.h"
#include "varlink.h"

#include <string.h>

long varlink_address_get_type(const char *address, const char **parameterp) {
        const char *colon;
        unsigned long length;
        long type;

        colon = strchr(address, ':');
        if (colon == NULL || colon[1] == '\0')
                return -VARLINK_ERROR_INVALID_ADDRESS;

        length = colon - address;

        if (strncmp(address, "unix", length) == 0)
                type = VARLINK_ADDRESS_UNIX;
        else if (strncmp(address, "tcp", length) == 0)
                type = VARLINK_ADDRESS_TCP;
        else
                return -VARLINK_ERROR_INVALID_ADDRESS;

        if (parameterp)
                *parameterp = colon + 1;

        return type;
}
