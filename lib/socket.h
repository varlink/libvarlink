#pragma once

#include "varlink.h"

typedef struct VarlinkSocket VarlinkSocket;

struct VarlinkSocket {
        int fd;

        uint8_t *in;
        unsigned long in_start;
        unsigned long in_end;

        uint8_t *out;
        unsigned long out_start;
        unsigned long out_end;

        bool hup;
};

void varlink_socket_init(VarlinkSocket *socket, int fd);
void varlink_socket_deinit(VarlinkSocket *socket);

long varlink_socket_read(VarlinkSocket *socket, VarlinkObject **messagep);
long varlink_socket_write(VarlinkSocket *socket, VarlinkObject *message);

/*
 * Flushes the write buffer. Returns the amount of bytes that are still
 * in the buffer.
 */
long varlink_socket_flush(VarlinkSocket *socket);

int varlink_connect(const char *address);
int varlink_accept(const char *address, int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp);

int varlink_connect_unix(const char *path);
int varlink_listen_unix(const char *path);
int varlink_accept_unix(int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp);

int varlink_connect_tcp(const char *address);
int varlink_listen_tcp(const char *address);
int varlink_accept_tcp(int listen_fd);
