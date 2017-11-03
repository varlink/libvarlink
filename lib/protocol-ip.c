#include "protocol.h"
#include "util.h"
#include "varlink.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>

int varlink_connect_ip(const char *address) {
        _cleanup_(freep) char *host = NULL;
        unsigned int port;
        char *colon;
        _cleanup_(closep) int fd = -1;
        struct hostent *server;
        struct sockaddr_in sa = {
                .sin_family = AF_INET,
        };
        int r;

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

        r = fd;
        fd = -1;

        return r;
}

int varlink_listen_ip(const char *address) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *host = NULL;
        unsigned int port;
        char *colon;
        struct sockaddr_in sa = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
        };
        int r;

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

        r = fd;
        fd = -1;

        return r;
}

int varlink_accept_ip(int listen_fd) {
        _cleanup_(closep) int fd = -1;
        _cleanup_(freep) char *address = NULL;
        int r;

        fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_ACCEPT;

        r = fd;
        fd = -1;

        return r;
}
