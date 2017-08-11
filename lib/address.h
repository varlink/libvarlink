#pragma once

enum {
        VARLINK_ADDRESS_UNIX,
        VARLINK_ADDRESS_TCP
};

long varlink_address_get_type(const char *address);
