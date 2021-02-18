// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "interface.h"
#include "varlink.h"

VarlinkInterface *varlink_service_get_interface_by_name(VarlinkService *service, const char *name);
