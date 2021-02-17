#include "transport.h"
#include "util.h"
#include "varlink.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>

static long strip_parameters(const char *address, char **hostp) {
        char *parm;
        _cleanup_(freep) char *host = NULL;

        parm = strchr(address, ';');
        if (!parm)
                host = strdup(address);
        else
                host = strndup(address, parm - address);
        if (!host)
                return -VARLINK_ERROR_PANIC;

        *hostp = host;
        host = NULL;
        return 0;
}

static void freeaddrinfop(struct addrinfo **ai) {
        if (*ai)
                freeaddrinfo(*ai);
}

static long resolve_addrinfo(const char *address, struct addrinfo **resultp) {
        _cleanup_(freep) char *host = NULL;
        char *endptr;
        char *port;
        struct addrinfo hints = {
                .ai_family = AF_UNSPEC,
                .ai_socktype = SOCK_STREAM,
                .ai_flags = AI_NUMERICSERV
        };
        _cleanup_(freeaddrinfop) struct addrinfo *result = NULL;

        /* Split host and port */
        port = strrchr(address, ':');
        if (!port)
                return -VARLINK_ERROR_INVALID_ADDRESS;

        host = strndup(address, port - address);
        if (!host)
                return -VARLINK_ERROR_PANIC;

        port += 1;

        /* Require decimal port specification */
        if (strtol(port, &endptr, 10) < 0 ||
            endptr == port || endptr[0] != '\0')
                return -VARLINK_ERROR_INVALID_ADDRESS;

        /* IPv6 literal addresses must be enclosed in [] */
        if (strchr(host, ':')) {
                char *s, *p;

                if (host[0] != '[')
                        return -VARLINK_ERROR_INVALID_ADDRESS;

                p = strchr(host, ']');
                if (!p || p[1] != '\0')
                        return -VARLINK_ERROR_INVALID_ADDRESS;

                s = strndup(host + 1, p - host - 1);
                if (!s)
                        return -VARLINK_ERROR_PANIC;

                free(host);
                host = s;

                hints.ai_family = AF_INET6;
        }

        if (getaddrinfo(host, port, &hints, &result) != 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (result->ai_family != AF_INET &&
            result->ai_family != AF_INET6)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        *resultp = result;
        result = NULL;

        return 0;
}

int varlink_connect_tcp(const char *address) {
        _cleanup_(freep) char *host = NULL;
        _cleanup_(freeaddrinfop) struct addrinfo *result = NULL;
        _cleanup_(closep) int fd = -1;
        int r;

        r = strip_parameters(address, &host);
        if (r < 0)
                return r;

        r = resolve_addrinfo(host, &result);
        if (r < 0)
                return r;

        fd = socket(result->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        if (connect(fd, result->ai_addr, result->ai_addrlen) < 0 && errno != EINPROGRESS)
                return -VARLINK_ERROR_CANNOT_CONNECT;

        r = fd;
        fd = -1;

        return r;
}

int varlink_listen_tcp(const char *address) {
        _cleanup_(freep) char *host = NULL;
        _cleanup_(closep) int fd = -1;
        const int on = 1;
        _cleanup_(freeaddrinfop) struct addrinfo *result = NULL;
        int r;

        r = strip_parameters(address, &host);
        if (r < 0)
                return r;

        r = resolve_addrinfo(host, &result);
        if (r < 0)
                return r;

        fd = socket(result->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (bind(fd, result->ai_addr, result->ai_addrlen))
                return -VARLINK_ERROR_CANNOT_LISTEN;

        if (listen(fd, SOMAXCONN) < 0)
                return -VARLINK_ERROR_CANNOT_LISTEN;

        r = fd;
        fd = -1;

        return r;
}

int varlink_accept_tcp(int listen_fd) {
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
