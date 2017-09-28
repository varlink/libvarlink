#include "stream.h"

#include "object.h"
#include "util.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define CONNECTION_BUFFER_SIZE (16 * 1024 * 1024)

void varlink_stream_init(VarlinkStream *stream, int fd) {
        stream->fd = fd;

        stream->in = malloc(CONNECTION_BUFFER_SIZE);
        stream->in_start = 0;
        stream->in_end = 0;

        stream->out = malloc(CONNECTION_BUFFER_SIZE);
        stream->out_start = 0;
        stream->out_end = 0;

        stream->hup = false;
}

void varlink_stream_deinit(VarlinkStream *stream) {
        if (stream->fd >= 0)
                close(stream->fd);

        free(stream->in);
        free(stream->out);

        memset(stream, 0, sizeof(VarlinkStream));
        stream->fd = -1;
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
                        if (errno != EAGAIN)
                                return -VARLINK_ERROR_SENDING_MESSAGE;
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
                                if (errno == EAGAIN)
                                        return 0;

                                return -VARLINK_ERROR_RECEIVING_MESSAGE;

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

        length = varlink_object_to_pretty_json(message, &json, -1, NULL, NULL, NULL, NULL);
        if (length < 0)
                return length;

        if (length >= CONNECTION_BUFFER_SIZE - 1)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        if (stream->out_end + length + 1 >= CONNECTION_BUFFER_SIZE)
                return -VARLINK_ERROR_SENDING_MESSAGE;

        memcpy(stream->out + stream->out_end, json, length + 1);
        stream->out_end += length + 1;

        return 0;
}

