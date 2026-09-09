// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "varlink.h"

#define VARLINK_BUFFER_FDS_MAX 1024

typedef struct VarlinkStream VarlinkStream;
typedef struct VarlinkStreamFds VarlinkStreamFds;

/* A batch of outgoing descriptors, anchored at a position in the output byte stream */
struct VarlinkStreamFds {
        unsigned long position;
        int *fds;
        unsigned long n_fds;

        VarlinkStreamFds *next;
};

struct VarlinkStream {
        int fd;

        uint8_t *in;
        unsigned long in_start;
        unsigned long in_end;

        uint8_t *out;
        unsigned long out_start;
        unsigned long out_end;

        bool hup;

        bool allow_fd_passing_in;
        bool allow_fd_passing_out;

        /* descriptors that arrived with the message last returned by varlink_stream_read() */
        int *in_fds;
        unsigned long n_in_fds;

        VarlinkStreamFds *out_fds;

        /* total bytes ever sent from out */
        unsigned long out_sent;
};

long varlink_stream_new(VarlinkStream **streamp, int fd, bool allow_fd_passing_in);
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
 * Like varlink_stream_write(), but sends n_fds descriptors along with the
 * message. Always takes ownership of the descriptors and the array.
 */
long varlink_stream_write_with_fds(VarlinkStream *stream, VarlinkObject *message,
                                   int *fds, unsigned long n_fds);

/*
 * Flushes the write buffer. Returns the amount of bytes that are still
 * in the buffer.
 */
long varlink_stream_flush(VarlinkStream *stream);

long varlink_stream_set_allow_fd_passing_input(VarlinkStream *stream, bool enable);
long varlink_stream_set_allow_fd_passing_output(VarlinkStream *stream, bool enable);

/*
 * Access the descriptors that arrived with the message last returned by
 * varlink_stream_read(). take() passes ownership to the caller and leaves
 * the slot empty.
 */
long varlink_stream_get_n_in_fds(VarlinkStream *stream);
int varlink_stream_peek_in_fd(VarlinkStream *stream, unsigned long index);
int varlink_stream_take_in_fd(VarlinkStream *stream, unsigned long index);

/* Passes the whole set of received descriptors and its array to the caller */
void varlink_stream_take_in_fds(VarlinkStream *stream, int **fdsp, unsigned long *n_fdsp);

void varlink_stream_close_in_fds(VarlinkStream *stream);

long varlink_stream_bridge(int signal_fd, VarlinkStream *client_in, VarlinkStream *client_out, VarlinkStream *server);
