#include "address.h"
#include "varlink.h"

#include <string.h>

long varlink_address_get_type(const char *address) {
        switch (address[0]) {
                case '\0':
                        return -VARLINK_ERROR_INVALID_ADDRESS;

                case '/':
                case '@':
                        return VARLINK_ADDRESS_UNIX;

                default:
                        return VARLINK_ADDRESS_TCP;
        }
}
