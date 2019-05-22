#pragma once

#include "varlink.h"

typedef struct VarlinkStream VarlinkStream;

struct VarlinkStream {
        int fd;

        uint8_t *in;
        unsigned long in_start;
        unsigned long in_end;

        uint8_t *out;
        unsigned long out_start;
        unsigned long out_end;

        bool hup;
};

long varlink_stream_new(VarlinkStream **streamp, int fd);
VarlinkStream *varlink_stream_free(VarlinkStream *stream);

/*
 * Reads a message from the stream. If a full message is available,
 * return 1 and store it in messagep. Otherwise, returns 0.
 */
long varlink_stream_read(VarlinkStream *stream, VarlinkObject **messagep);

/*
 * Writes message to the stream. Returns 1 if the whole message was
 * written. Otherwise, returns 0. Use varlink_stream_flush() to write
 * the remaining data once the underlying fd becomes writable again.
 */
long varlink_stream_write(VarlinkStream *stream, VarlinkObject *message);

/*
 * Flushes the write buffer. Returns the amount of bytes that are still
 * in the buffer.
 */
long varlink_stream_flush(VarlinkStream *stream);

long varlink_stream_bridge(int signal_fd, VarlinkStream *client_in, VarlinkStream *client_out, VarlinkStream *server);
