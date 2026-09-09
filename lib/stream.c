// SPDX-License-Identifier: Apache-2.0

#include "stream.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#ifndef SO_PASSRIGHTS
#define SO_PASSRIGHTS 83
#endif

#define CONNECTION_BUFFER_SIZE (16 * 1024 * 1024)

#define CONTROL_BUFFER_SIZE (CMSG_SPACE(sizeof(int) * VARLINK_BUFFER_FDS_MAX) + CMSG_SPACE(sizeof(struct ucred)))

/* Best effort: not a socket, or a kernel without the option */
static void varlink_stream_set_passrights(int fd, bool enable) {
        int on = enable;

        (void) setsockopt(fd, SOL_SOCKET, SO_PASSRIGHTS, &on, sizeof(on));
}

static VarlinkStreamFds *varlink_stream_fds_free(VarlinkStreamFds *batch) {
        close_and_free_fds(&batch->fds, &batch->n_fds);
        free(batch);

        return NULL;
}

long varlink_stream_new(VarlinkStream **streamp, int fd, bool allow_fd_passing_in) {
        _cleanup_(freep) VarlinkStream *stream = NULL;

        stream = calloc(1, sizeof(VarlinkStream));
        if (!stream)
                return -VARLINK_ERROR_PANIC;

        stream->fd = fd;
        stream->allow_fd_passing_in = allow_fd_passing_in;

        /* Set once, so that a peer sending descriptors immediately after
         * connecting cannot land on a socket that still refuses them */
        varlink_stream_set_passrights(fd, allow_fd_passing_in);

        stream->in = malloc(CONNECTION_BUFFER_SIZE);
        if (!stream->in)
                return -VARLINK_ERROR_PANIC;

        stream->out = malloc(CONNECTION_BUFFER_SIZE);
        if (!stream->out) {
                free(stream->in);
                return -VARLINK_ERROR_PANIC;
        }

        *streamp = stream;
        stream = NULL;

        return 0;
}

VarlinkStream *varlink_stream_free(VarlinkStream *stream) {
        if (stream->fd >= 0)
                close(stream->fd);

        varlink_stream_close_in_fds(stream);

        while (stream->out_fds) {
                VarlinkStreamFds *batch = stream->out_fds;

                stream->out_fds = batch->next;
                varlink_stream_fds_free(batch);
        }

        free(stream->in);
        free(stream->out);

        free(stream);
        return NULL;
}

long varlink_stream_set_allow_fd_passing_input(VarlinkStream *stream, bool enable) {
        if (stream->allow_fd_passing_in == enable)
                return 0;

        varlink_stream_set_passrights(stream->fd, enable);

        stream->allow_fd_passing_in = enable;

        if (!enable)
                varlink_stream_close_in_fds(stream);

        return 0;
}

long varlink_stream_set_allow_fd_passing_output(VarlinkStream *stream, bool enable) {
        stream->allow_fd_passing_out = enable;

        return 0;
}

long varlink_stream_get_n_in_fds(VarlinkStream *stream) {
        return (long) stream->n_in_fds;
}

int varlink_stream_peek_in_fd(VarlinkStream *stream, unsigned long index) {
        if (index >= stream->n_in_fds || stream->in_fds[index] < 0)
                return -VARLINK_ERROR_INVALID_INDEX;

        return stream->in_fds[index];
}

int varlink_stream_take_in_fd(VarlinkStream *stream, unsigned long index) {
        int fd;

        if (index >= stream->n_in_fds || stream->in_fds[index] < 0)
                return -VARLINK_ERROR_INVALID_INDEX;

        fd = stream->in_fds[index];
        stream->in_fds[index] = -1;

        return fd;
}

void varlink_stream_take_in_fds(VarlinkStream *stream, int **fdsp, unsigned long *n_fdsp) {
        *fdsp = stream->in_fds;
        *n_fdsp = stream->n_in_fds;

        stream->in_fds = NULL;
        stream->n_in_fds = 0;
}

void varlink_stream_close_in_fds(VarlinkStream *stream) {
        close_and_free_fds(&stream->in_fds, &stream->n_in_fds);
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

static long varlink_stream_send(VarlinkStream *stream, VarlinkStreamFds *batch) {
        unsigned long length = stream->out_end - stream->out_start;
        uint8_t control[CONTROL_BUFFER_SIZE];
        struct cmsghdr *cmsg;
        struct iovec iov;
        struct msghdr mh = {};

        if (!batch || batch->position > stream->out_sent) {
                if (batch)
                        length = MIN(length, batch->position - stream->out_sent);

                return write(stream->fd, stream->out + stream->out_start, length);
        }

        /* stop before the next batch, its descriptors belong to a later message */
        if (batch->next)
                length = MIN(length, batch->next->position - stream->out_sent);

        iov.iov_base = stream->out + stream->out_start;
        iov.iov_len = length;

        memset(control, 0, CMSG_SPACE(sizeof(int) * batch->n_fds));
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = control;
        mh.msg_controllen = CMSG_SPACE(sizeof(int) * batch->n_fds);

        cmsg = CMSG_FIRSTHDR(&mh);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * batch->n_fds);
        memcpy(CMSG_DATA(cmsg), batch->fds, sizeof(int) * batch->n_fds);

        return sendmsg(stream->fd, &mh, MSG_NOSIGNAL);
}

long varlink_stream_flush(VarlinkStream *stream) {
        while (stream->out_end > stream->out_start) {
                VarlinkStreamFds *batch = stream->out_fds;
                bool sent_fds = batch && batch->position <= stream->out_sent;
                long n;

                n = varlink_stream_send(stream, batch);
                if (n < 0) {
                        switch (errno) {
                                case EINTR:
                                        continue;

                                case EAGAIN:
                                        // this function returns the number of bytes still to send
                                        goto out;

                                case EPIPE:
                                        stream->hup = true;
                                        return -VARLINK_ERROR_CONNECTION_CLOSED;

                                default:
                                        return -VARLINK_ERROR_SENDING_MESSAGE;
                        }
                }

                stream->out_start += n;
                stream->out_sent += n;

                if (sent_fds) {
                        stream->out_fds = batch->next;
                        varlink_stream_fds_free(batch);
                }
        }

out:
        move_rest(&stream->out, &stream->out_start, &stream->out_end);
        return (long) (stream->out_end - stream->out_start);
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

static long fd_block(int fd) {
        int flags;

        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
                return -errno;

        if ((flags & O_NONBLOCK) == 0)
                return 0;

        if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0)
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
                                goto bridge_out;

                        out = *(int *) ev[i].data.ptr;

                        if (out == server->fd)
                                in = client_in->fd;
                        else if (out == client_out->fd) {
                                in = server->fd;
                        } else {
                                goto bridge_out;
                        }
read_again:
                        r = read(in, buf, sizeof buf);
                        if (r == 0)
                                goto bridge_out;

                        if (r < 0) {
                                switch (errno) {
                                        case EINTR:
                                                goto read_again;

                                        case EAGAIN:
                                                // The next epoll will let us retry
                                                continue;

                                        default:
                                                goto bridge_out;
                                }
                        }

                        fd_block(out);
                        towrite = (size_t) r;
                        while (towrite) {
                                r = write(out, buf, towrite);
                                if (r < 0)
                                        switch (errno) {
                                                case EINTR:
                                                        continue;

                                                case EAGAIN:
                                                        // Retry until success, we don't want to cache
                                                        usleep(10000);
                                                        continue;

                                                default:
                                                        fd_nonblock(out);
                                                        goto bridge_out;
                                        }
                                towrite -= r;
                        }
                        fd_nonblock(out);

                        if (ev[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
                                goto bridge_out;
                }
        }
bridge_out:
        return 0;
}

/* Stores the result of the recvmsg() in np, returns a VARLINK_ERROR for the descriptors */
static long varlink_stream_recv(VarlinkStream *stream, long *np) {
        uint8_t control[CONTROL_BUFFER_SIZE];
        struct cmsghdr *cmsg;
        struct iovec iov;
        struct msghdr mh = {};
        long r = 0;

        iov.iov_base = stream->in + stream->in_end;
        iov.iov_len = CONNECTION_BUFFER_SIZE - stream->in_end;

        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = control;
        mh.msg_controllen = sizeof(control);

        *np = recvmsg(stream->fd, &mh, MSG_CMSG_CLOEXEC);
        if (*np <= 0)
                return 0;

        for (cmsg = CMSG_FIRSTHDR(&mh); cmsg; cmsg = CMSG_NXTHDR(&mh, cmsg)) {
                unsigned long n_fds;
                int *fds;

                if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
                        continue;

                n_fds = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);

                /* Truncation drops descriptors and shifts every index behind them */
                if (mh.msg_flags & MSG_CTRUNC) {
                        close_fds((int *) CMSG_DATA(cmsg), n_fds);
                        r = -VARLINK_ERROR_INVALID_MESSAGE;
                        continue;
                }

                /* Descriptors only ever accompany the first byte of a message */
                if (stream->in_end > 0) {
                        close_fds((int *) CMSG_DATA(cmsg), n_fds);
                        return -VARLINK_ERROR_INVALID_MESSAGE;
                }

                if (stream->n_in_fds + n_fds > VARLINK_BUFFER_FDS_MAX) {
                        close_fds((int *) CMSG_DATA(cmsg), n_fds);
                        return -VARLINK_ERROR_INVALID_MESSAGE;
                }

                fds = realloc(stream->in_fds, sizeof(int) * (stream->n_in_fds + n_fds));
                if (!fds) {
                        close_fds((int *) CMSG_DATA(cmsg), n_fds);
                        return -VARLINK_ERROR_PANIC;
                }

                memcpy(fds + stream->n_in_fds, CMSG_DATA(cmsg), sizeof(int) * n_fds);
                stream->in_fds = fds;
                stream->n_in_fds += n_fds;
        }

        return r;
}

long varlink_stream_read(VarlinkStream *stream, VarlinkObject **messagep) {
        varlink_stream_close_in_fds(stream);

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
again:
                if (stream->allow_fd_passing_in) {
                        r = varlink_stream_recv(stream, &n);
                        if (r < 0)
                                return r;
                } else
                        n = read(stream->fd,
                                 stream->in + stream->in_end,
                                 CONNECTION_BUFFER_SIZE - stream->in_end);

                switch (n) {
                        case -1:
                                switch (errno) {
                                        case EINTR:
                                                goto again;

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
                                /* should not be reached */
#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
                                asm("unreachable\n");
#pragma clang diagnostic pop

                        /* fall through */
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
#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCode"
        asm("unreachable\n");
#pragma clang diagnostic pop
}

long varlink_stream_write(VarlinkStream *stream, VarlinkObject *message) {
        return varlink_stream_write_with_fds(stream, message, NULL, 0);
}

long varlink_stream_write_with_fds(VarlinkStream *stream, VarlinkObject *message,
                                   int *fds, unsigned long n_fds) {
        _cleanup_(freep) char *json = NULL;
        VarlinkStreamFds *batch;
        VarlinkStreamFds **tailp;
        long length;
        unsigned long ulength;
        long r;

        if (n_fds > 0 && !stream->allow_fd_passing_out) {
                r = -VARLINK_ERROR_INVALID_CALL;
                goto fail;
        }

        length = varlink_object_to_json(message, &json);
        if (length < 0) {
                r = length;
                goto fail;
        }

        ulength = (unsigned long) length;

        if (ulength >= CONNECTION_BUFFER_SIZE - 1) {
                r = -VARLINK_ERROR_INVALID_MESSAGE;
                goto fail;
        }

        if (stream->out_end + ulength + 1 >= CONNECTION_BUFFER_SIZE) {
                r = -VARLINK_ERROR_SENDING_MESSAGE;
                goto fail;
        }

        if (n_fds > 0) {
                batch = calloc(1, sizeof(VarlinkStreamFds));
                if (!batch) {
                        r = -VARLINK_ERROR_PANIC;
                        goto fail;
                }

                batch->position = stream->out_sent + (stream->out_end - stream->out_start);
                batch->fds = fds;
                batch->n_fds = n_fds;

                for (tailp = &stream->out_fds; *tailp; tailp = &(*tailp)->next)
                        ;
                *tailp = batch;
        }

        memcpy(stream->out + stream->out_end, json, ulength + 1);
        stream->out_end += ulength + 1;

        r = varlink_stream_flush(stream);
        if (r < 0)
                return r;

        /* return 1 when flush() wrote the whole message */
        return r == 0 ? 1 : 0;

fail:
        close_and_free_fds(&fds, &n_fds);
        return r;
}
