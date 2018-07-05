#pragma once

#include "uri.h"
#include "varlink.h"

long varlink_connection_new_from_fd(VarlinkConnection **connectionp, int fd);
long varlink_connection_new_from_uri(VarlinkConnection **connectionp, VarlinkURI *uri);
