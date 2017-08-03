#pragma once

#include "interface.h"
#include "varlink.h"

long varlink_server_get_interface_by_name(VarlinkServer *server,
                                          VarlinkInterface **interfacep,
                                          const char *name);

