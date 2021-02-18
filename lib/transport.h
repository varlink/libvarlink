// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "uri.h"
#include <stdlib.h>

int varlink_transport_listen(VarlinkURI *uri, char **pathp);
int varlink_transport_accept(VarlinkURI *uri, int listen_fd);
int varlink_transport_connect(VarlinkURI *uri);

int varlink_connect_device(const char *device);

int varlink_listen_tcp(const char *address);
int varlink_accept_tcp(int listen_fd);
int varlink_connect_tcp(const char *address);

int varlink_listen_unix(const char *address, char **pathp);
int varlink_accept_unix(int listen_fd);
int varlink_connect_unix(const char *address);
