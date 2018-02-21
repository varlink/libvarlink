#include "stream.h"

#include "object.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/wait.h>

#define CONNECTION_BUFFER_SIZE (16 * 1024 * 1024)

long varlink_stream_new(VarlinkStream **streamp, int fd, pid_t pid) {
        VarlinkStream *stream;

        stream = calloc(1, sizeof(VarlinkStream));
        if (!stream)
                return -VARLINK_ERROR_PANIC;

        stream->fd = fd;
        stream->pid = pid;

        stream->in = malloc(CONNECTION_BUFFER_SIZE);
        if (!stream->in)
                return -VARLINK_ERROR_PANIC;

        stream->out = malloc(CONNECTION_BUFFER_SIZE);
        if (!stream->out)
                return -VARLINK_ERROR_PANIC;

        *streamp = stream;
        return 0;
}

VarlinkStream *varlink_stream_free(VarlinkStream *stream) {
        if (stream->fd >= 0)
                close(stream->fd);

        free(stream->in);
        free(stream->out);

        if (stream->pid > 0 && kill(stream->pid, SIGTERM) >= 0)
                waitpid(stream->pid, NULL, 0);

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

long varlink_stream_read(VarlinkStream *stream, VarlinkObject **messagep) {
        for (;;) {
                uint8_t *nul;
                long r, n;

                nul = memchr(&stream->in[stream->in_start], 0, stream->in_end - stream->in_start);
                if (nul) {
                        r = varlink_object_new_from_json(messagep, (const char *)&stream->in[stream->in_start]);
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

        length = varlink_object_to_pretty_json(message, &json, -1, NULL, NULL, NULL, NULL);
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
