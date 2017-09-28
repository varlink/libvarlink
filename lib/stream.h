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

void varlink_stream_init(VarlinkStream *stream, int fd);
void varlink_stream_deinit(VarlinkStream *stream);

long varlink_stream_read(VarlinkStream *stream, VarlinkObject **messagep);
long varlink_stream_write(VarlinkStream *stream, VarlinkObject *message);

/*
 * Flushes the write buffer. Returns the amount of bytes that are still
 * in the buffer.
 */
long varlink_stream_flush(VarlinkStream *stream);

