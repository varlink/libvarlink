#pragma once

#include <sys/socket.h>

int varlink_connect(const char *address, pid_t *pidp);
int varlink_accept(const char *address,
                   int listen_fd,
                   mode_t mode,
                   pid_t *pidp, uid_t *uidp, gid_t *gidp);

int varlink_connect_unix(const char *path);
int varlink_listen_unix(const char *path, char **pathp);
int varlink_accept_unix(int listen_fd,
                        mode_t mode,
                        pid_t *pidp, uid_t *uidp, gid_t *gidp);

int varlink_connect_ip(const char *address);
int varlink_listen_ip(const char *address);
int varlink_accept_ip(int listen_fd);

int varlink_connect_ssh(const char *address, pid_t *pidp);
int varlink_connect_exec(const char *executable, pid_t *pidp);
