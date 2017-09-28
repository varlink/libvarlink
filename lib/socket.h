#pragma once

#include <sys/socket.h>

int varlink_connect(const char *address);
int varlink_accept(const char *address, int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp);

int varlink_connect_unix(const char *path);
int varlink_listen_unix(const char *path);
int varlink_accept_unix(int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp);

int varlink_connect_tcp(const char *address);
int varlink_listen_tcp(const char *address);
int varlink_accept_tcp(int listen_fd);
