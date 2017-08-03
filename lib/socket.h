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

void varlink_socket_init(VarlinkSocket *socket);
void varlink_socket_deinit(VarlinkSocket *socket);

long varlink_socket_dispatch(VarlinkSocket *socket, int events);
int varlink_socket_get_events(VarlinkSocket *socket);

long varlink_socket_read(VarlinkSocket *socket, VarlinkObject **messagep);
long varlink_socket_write(VarlinkSocket *socket, VarlinkObject *message);

long varlink_socket_connect_unix(VarlinkSocket *socket, const char *path);
long varlink_socket_listen_unix(const char *path, int *fdp);
long varlink_socket_accept_unix(int listen_fd, VarlinkSocket *socket, VarlinkObject **credentialsp);

long varlink_socket_connect_tcp(VarlinkSocket *socket, const char *address);
long varlink_socket_listen_tcp(const char *address, int *fdp);
long varlink_socket_accept_tcp(int listen_fd, VarlinkSocket *socket, VarlinkObject **credentialsp);
