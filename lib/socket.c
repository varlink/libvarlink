#include "socket.h"

#include "object.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define CONNECTION_BUFFER_SIZE (16 * 1024 * 1024)

enum {
        VARLINK_ADDRESS_UNIX,
        VARLINK_ADDRESS_TCP
};

static long varlink_address_get_type(const char *address) {
        switch (address[0]) {
                case '\0':
                        return -VARLINK_ERROR_INVALID_ADDRESS;

                case '/':
                case '@':
                        return VARLINK_ADDRESS_UNIX;

                default:
                        return VARLINK_ADDRESS_TCP;
        }
}

void varlink_socket_init(VarlinkSocket *socket, int fd) {
        socket->fd = fd;

        socket->in = malloc(CONNECTION_BUFFER_SIZE);
        socket->in_start = 0;
        socket->in_end = 0;

        socket->out = malloc(CONNECTION_BUFFER_SIZE);
        socket->out_start = 0;
        socket->out_end = 0;

        socket->hup = false;
}

void varlink_socket_deinit(VarlinkSocket *socket) {
        if (socket->fd >= 0)
                close(socket->fd);

        free(socket->in);
        free(socket->out);

        memset(socket, 0, sizeof(VarlinkSocket));
        socket->fd = -1;
}

static void move_rest(uint8_t **bufferp, unsigned long *startp, unsigned long *endp) {
        uint8_t *buffer;
        unsigned long start, end, rest;

        buffer = *bufferp;
        start = *startp;
        end = *endp;

        rest = end - start;
        if (rest > 0)
                *bufferp = memmove(buffer, buffer + start, rest);

        *startp = 0;
        *endp = rest;
}

long varlink_socket_flush(VarlinkSocket *socket) {
        long n;

        n = write(socket->fd,
                  socket->out + socket->out_start,
                  socket->out_end - socket->out_start);

        switch (n) {
                case -1:
                        if (errno != EAGAIN)
                                return -VARLINK_ERROR_SENDING_MESSAGE;
                        break;
                default:
                        socket->out_start += n;
                        break;
        }

        move_rest(&socket->out, &socket->out_start, &socket->out_end);

        return socket->out_end - socket->out_start;
}

long varlink_socket_read(VarlinkSocket *socket, VarlinkObject **messagep) {
        for (;;) {
                uint8_t *nul;
                long r, n;

                nul = memchr(&socket->in[socket->in_start], 0, socket->in_end - socket->in_start);
                if (nul) {
                        r = varlink_object_new_from_json(messagep, (const char *)&socket->in[socket->in_start]);
                        if (r < 0)
                                return r;

                        socket->in_start = (nul + 1) - socket->in;
                        return 1;
                }

                move_rest(&socket->in, &socket->in_start, &socket->in_end);

                if (socket->in_end == CONNECTION_BUFFER_SIZE)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                n = read(socket->fd,
                         socket->in + socket->in_end,
                         CONNECTION_BUFFER_SIZE - socket->in_end);

                switch (n) {
                        case -1:
                                if (errno == EAGAIN)
                                        return 0;

                                return -VARLINK_ERROR_RECEIVING_MESSAGE;

                        case 0:
                                socket->hup = true;
                                *messagep = NULL;
                                return 0;

                        default:
                                socket->in_end += n;
                                break;
                }
        }

        /* should not be reached */
        return -VARLINK_ERROR_PANIC;
}

long varlink_socket_write(VarlinkSocket *socket, VarlinkObject *message) {
        _cleanup_(freep) char *json = NULL;
        long length;

        length = varlink_object_to_pretty_json(message, &json, -1, NULL, NULL, NULL, NULL);
        if (length < 0)
                return length;

        if (length >= CONNECTION_BUFFER_SIZE - 1)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        if (socket->out_end + length + 1 >= CONNECTION_BUFFER_SIZE)
                return -VARLINK_ERROR_SENDING_MESSAGE;

        memcpy(socket->out + socket->out_end, json, length + 1);
        socket->out_end += length + 1;

        return 0;
}

int varlink_connect(const char *address) {
        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        return varlink_connect_unix(address);

                case VARLINK_ADDRESS_TCP:
                        return varlink_connect_tcp(address);

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }
}

int varlink_accept(const char *address, int listen_fd, pid_t *pidp, uid_t *uidp, gid_t *gidp) {
        int fd;

        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        return varlink_accept_unix(listen_fd, pidp, uidp, gidp);

                case VARLINK_ADDRESS_TCP:
                        fd = varlink_accept_tcp(listen_fd);
                        if (fd < 0)
                                return fd;

                        *pidp = (pid_t)-1;
                        *uidp = (uid_t)-1;
                        *gidp = (gid_t)-1;
                        return fd;

                default:
                        return -VARLINK_ERROR_PANIC;
        }
}

_public_ int varlink_listen(const char *address, char **pathp) {
        const char *path = NULL;
        int fd;

        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        /* abstract namespace */
                        if (address[0] != '@')
                                path = address;

                        fd = varlink_listen_unix(address);
                        break;

                case VARLINK_ADDRESS_TCP:
                        fd = varlink_listen_tcp(address);
                        break;

                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        /* CannotListen or InvalidAddress */
        if (fd < 0)
                return fd;

        if (pathp)
                *pathp = path ? strdup(path) : NULL;

        return fd;
}
