#include "object.h"
#include "connection.h"
#include "stream.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>

#define CONNECTION_BUFFER_SIZE (16 * 1024 * 1024)

long varlink_stream_new(VarlinkStream **streamp, int fd) {
        _cleanup_(freep) VarlinkStream *stream = NULL;

        stream = calloc(1, sizeof(VarlinkStream));
        if (!stream)
                return -VARLINK_ERROR_PANIC;

        stream->fd = fd;

        stream->in = malloc(CONNECTION_BUFFER_SIZE);
        if (!stream->in)
                return -VARLINK_ERROR_PANIC;

        stream->out = malloc(CONNECTION_BUFFER_SIZE);
        if (!stream->out)
                return -VARLINK_ERROR_PANIC;

        *streamp = stream;
        stream = NULL;

        return 0;
}

VarlinkStream *varlink_stream_free(VarlinkStream *stream) {
        if (stream->fd >= 0)
                close(stream->fd);

        free(stream->in);
        free(stream->out);

        free(stream);
        return NULL;
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

long varlink_stream_flush(VarlinkStream *stream) {
        long n;

        n = write(stream->fd,
                  stream->out + stream->out_start,
                  stream->out_end - stream->out_start);

        switch (n) {
                case -1:
                        switch (errno) {
                                case EAGAIN:
                                        break;

                                case EPIPE:
                                        stream->hup = true;
                                        return -VARLINK_ERROR_CONNECTION_CLOSED;

                                default:
                                        return -VARLINK_ERROR_SENDING_MESSAGE;
                        }
                        break;

                default:
                        stream->out_start += n;
                        break;
        }

        move_rest(&stream->out, &stream->out_start, &stream->out_end);
        return stream->out_end - stream->out_start;
}

static long fd_nonblock(int fd) {
        int flags;

        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
                return -errno;

        if (flags & O_NONBLOCK)
                return 0;

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
                return -errno;

        return 0;
}

long varlink_stream_bridge(int signal_fd, VarlinkStream *client_in, VarlinkStream *client_out, VarlinkStream *server) {
        long r;
        unsigned char buf[8192];
        int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

        if (fd_nonblock(client_in->fd) < 0)
                return -1;
        if (fd_nonblock(client_out->fd) < 0)
                return -1;
        if (fd_nonblock(server->fd) < 0)
                return -1;

        epoll_add(epoll_fd, client_in->fd, EPOLLIN, (void *) &server->fd);
        epoll_add(epoll_fd, server->fd, EPOLLIN, (void *) &client_out->fd);
        epoll_add(epoll_fd, signal_fd, EPOLLIN, (void *) &signal_fd);

        for (;;) {
                struct epoll_event ev[3];
                int in, out;
                size_t towrite;
                int num_ev;

                num_ev = epoll_wait(epoll_fd, ev, 3, -1);
                if (num_ev < 0)
                        break;

                for (int i = 0; i < num_ev; i++) {
                        if (!(ev[i].events & EPOLLIN))
                                goto out;

                        out = *(int *) ev[i].data.ptr;

                        if (out == server->fd)
                                in = client_in->fd;
                        else if (out == client_out->fd) {
                                in = server->fd;
                        } else {
                                goto out;
                        }

                        r = read(in, buf, 8192);
                        if (r <= 0)
                                goto out;

                        towrite = r;
                        while (towrite) {
                                r = write(out, buf, towrite);
                                if (r <= 0)
                                        goto out;
                                towrite -= r;
                        }

                        if (ev[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
                                goto out;
                }
        }
        out:
        return 0;
}

long varlink_stream_read(VarlinkStream *stream, VarlinkObject **messagep) {
        for (;;) {
                uint8_t *nul;
                long r, n;

                nul = memchr(&stream->in[stream->in_start], 0, stream->in_end - stream->in_start);
                if (nul) {
                        r = varlink_object_new_from_json(messagep, (const char *) &stream->in[stream->in_start]);
                        if (r < 0)
                                return r;

                        stream->in_start = (nul + 1) - stream->in;
                        return 1;
                }

                move_rest(&stream->in, &stream->in_start, &stream->in_end);

                if (stream->in_end == CONNECTION_BUFFER_SIZE)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                n = read(stream->fd,
                         stream->in + stream->in_end,
                         CONNECTION_BUFFER_SIZE - stream->in_end);

                switch (n) {
                        case -1:
                                switch (errno) {
                                        case EAGAIN:
                                                *messagep = NULL;
                                                return 0;

                                        case ECONNRESET:
                                                stream->hup = true;
                                                *messagep = NULL;
                                                return 0;

                                        default:
                                                return -VARLINK_ERROR_RECEIVING_MESSAGE;
                                }
                                break;

                        case 0:
                                stream->hup = true;
                                *messagep = NULL;
                                return 0;

                        default:
                                stream->in_end += n;
                                break;
                }
        }

        /* should not be reached */
        return -VARLINK_ERROR_PANIC;
}

long varlink_stream_write(VarlinkStream *stream, VarlinkObject *message) {
        _cleanup_(freep) char *json = NULL;
        long length;
        long r;

        length = varlink_object_to_json(message, &json);
        if (length < 0)
                return length;

        if (length >= CONNECTION_BUFFER_SIZE - 1)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        if (stream->out_end + length + 1 >= CONNECTION_BUFFER_SIZE)
                return -VARLINK_ERROR_SENDING_MESSAGE;

        memcpy(stream->out + stream->out_end, json, length + 1);
        stream->out_end += length + 1;

        r = varlink_stream_flush(stream);
        if (r < 0)
                return r;

        /* return 1 when flush() wrote the whole message */
        return r == 0 ? 1 : 0;
}
