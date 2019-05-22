#pragma once

#include "uri.h"
#include "varlink.h"
#include "stream.h"

long varlink_connection_new_from_fd(VarlinkConnection **connectionp, int fd);
long varlink_connection_new_from_uri(VarlinkConnection **connectionp, VarlinkURI *uri);
long varlink_connection_bridge(int signal_fd, VarlinkStream *client_in, VarlinkStream *client_out, VarlinkConnection *server);
