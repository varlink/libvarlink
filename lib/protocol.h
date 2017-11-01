#pragma once

#include "uri.h"
#include <stdlib.h>

int varlink_protocol_listen(VarlinkURI *uri, char **pathp);
int varlink_protocol_accept(VarlinkURI *uri, int listen_fd);
int varlink_protocol_connect(VarlinkURI *uri, pid_t *pidp);

int varlink_listen_unix(const char *address, char **pathp);
int varlink_accept_unix(int listen_fd);
int varlink_connect_unix(const char *address);

int varlink_listen_ip(const char *address);
int varlink_accept_ip(int listen_fd);
int varlink_connect_ip(const char *address);

int varlink_connect_ssh(const char *address, pid_t *pidp);
int varlink_connect_exec(const char *executable, pid_t *pidp);
