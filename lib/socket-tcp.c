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

        fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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

long varlink_socket_accept_tcp(VarlinkSocket *socket, int listen_fd) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *address = NULL;

        assert(socket->fd == -1);

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        socket->fd = fd;
        fd = -1;

        return 0;
}
