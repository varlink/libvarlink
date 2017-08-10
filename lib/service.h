#pragma once

#include <varlink.h>

#include "interface.h"

VarlinkInterface *varlink_service_get_interface_by_name(VarlinkService *service, const char *name);
