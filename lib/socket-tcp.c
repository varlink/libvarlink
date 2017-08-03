#include "socket.h"
#include "util.h"

#include <assert.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>


long varlink_socket_connect_tcp(VarlinkSocket *vsocket, const char *address) {
        _cleanup_(freep) char *host = NULL;
        unsigned int port;
        char *colon;
        _cleanup_(closep) int fd = -1;
        struct hostent *server;
        struct sockaddr_in sa = {
                .sin_family = AF_INET,
        };

        assert(vsocket->fd == -1);

        colon = strrchr(address, ':');
        if (!colon)
                return -VARLINK_ERROR_INVALID_ADDRESS;

        host = strndup(address, colon - address);
        port = atoi(colon + 1);

        server = gethostbyname(host);
        if (!server)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        memcpy(&sa.sin_addr.s_addr, server->h_addr, server->h_length);
        sa.sin_port = htons(port);

        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        if (connect(fd, &sa, sizeof(sa)) < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        vsocket->fd = -1;
        fd = -1;

        return 0;
}

long varlink_socket_listen_tcp(const char *address, int *fdp) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *host = NULL;
        unsigned int port;
        char *colon;
        struct sockaddr_in sa = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
        };

        colon = strrchr(address, ':');
        if (!colon)
                return -VARLINK_ERROR_INVALID_ADDRESS;

        port = atoi(colon + 1);
        sa.sin_port = htons(port);

        fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)))
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (listen(fd, SOMAXCONN) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        *fdp = fd;
        fd = -1;

        return 0;
}

long varlink_socket_accept_tcp(int listen_fd, VarlinkSocket *socket, VarlinkObject **credentialsp) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *address = NULL;
        union {
                struct sockaddr sa;
                struct sockaddr_in in;
                struct sockaddr_in6 in6;
        } sa;
        socklen_t sa_len = sizeof(sa);

        assert(socket->fd == -1);

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        if (getpeername(fd, &sa.sa, &sa_len) < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        switch (sa.sa.sa_family) {
                case AF_INET: {
                        uint32_t addr;

                        addr = be32toh(sa.in.sin_addr.s_addr);
                        asprintf(&address,
                                 "tcp:%u.%u.%u.%u:%u",
                                 addr >> 24, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff,
                                 be16toh(sa.in.sin_port));

                        break;

                }

                case AF_INET6: {
                        char addr[INET6_ADDRSTRLEN];

                        inet_ntop(AF_INET6, &sa.in6.sin6_addr, addr, sizeof(addr));
                        asprintf(&address,
                                 "tcp:[%s]:%u",
                                 addr,
                                 be16toh(sa.in6.sin6_port));

                        break;
                }

                default:
                        return -VARLINK_ERROR_PANIC;
        }

        if (credentialsp) {
                VarlinkObject *credentials;

                varlink_object_new(&credentials);
                varlink_object_set_string(credentials, "address", address);

                *credentialsp = credentials;
        }

        socket->fd = fd;
        fd = -1;

        return 0;
}
