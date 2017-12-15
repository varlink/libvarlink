#include "connection.h"
#include "message.h"
#include "object.h"
#include "stream.h"
#include "transport.h"
#include "uri.h"
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/queue.h>
#include <unistd.h>

typedef struct ReplyCallback ReplyCallback;

struct ReplyCallback {
        uint64_t call_flags;
        VarlinkReplyFunc func;
        void *userdata;

        STAILQ_ENTRY(ReplyCallback) entry;
};

struct VarlinkConnection {
        VarlinkStream *stream;
        int events;

        STAILQ_HEAD(pending, ReplyCallback) pending;

        VarlinkConnectionClosedFunc closed_callback;
        void *closed_userdata;
};

long varlink_connection_new_from_uri(VarlinkConnection **connectionp, VarlinkURI *uri) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        long fd;
        pid_t pid = -1;
        long r;

        connection = calloc(1, sizeof(VarlinkConnection));
        if (!connection)
                return -VARLINK_ERROR_PANIC;

        connection->events = EPOLLIN;
        STAILQ_INIT(&connection->pending);

        fd = varlink_transport_connect(uri, &pid);
        if (fd < 0)
                return fd; /* CannotConnect or InvalidAddress */

        r = varlink_stream_new(&connection->stream, fd, pid);
        if (r < 0)
                return r;

        *connectionp = connection;
        connection = NULL;

        return 0;
}

_public_ long varlink_connection_new(VarlinkConnection **connectionp, const char *address) {
        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
        long r;

        r = varlink_uri_new(&uri, address, false);
        if (r < 0)
                return r;

        r = varlink_connection_new_from_uri(connectionp, uri);
        if (r < 0)
                return r;

        return 0;
}

_public_ VarlinkConnection *varlink_connection_free(VarlinkConnection *connection) {
        if (connection->stream)
                varlink_connection_close(connection);

        while (!STAILQ_EMPTY(&connection->pending)) {
                _cleanup_(freep) ReplyCallback *cb = NULL;

                cb = STAILQ_FIRST(&connection->pending);
                STAILQ_REMOVE_HEAD(&connection->pending, entry);
        }

        free(connection);

        return NULL;
}

_public_ void varlink_connection_freep(VarlinkConnection **connectionp) {
        if (*connectionp)
                varlink_connection_free(*connectionp);
}

_public_ long varlink_connection_process_events(VarlinkConnection *connection, int events) {
        long r = 0;

        if (!connection->stream)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        connection->events = EPOLLIN;

        if (events & EPOLLOUT) {
                r = varlink_stream_flush(connection->stream);
                if (r < 0)
                        return r;

                if (r > 0)
                        connection->events |= EPOLLOUT;
        }

        /* Check if the stream is valid, because a callback might have closed the connection */
        for (;;) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
                _cleanup_(freep) char *error = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
                uint64_t flags = 0;
                ReplyCallback *callback;

                r = varlink_stream_read(connection->stream, &message);
                if (r < 0)
                        return r;

                if (connection->stream->hup) {
                        connection->stream = varlink_stream_free(connection->stream);
                        return -VARLINK_ERROR_CONNECTION_CLOSED;
                }

                if (r == 0)
                        break;

                callback = STAILQ_FIRST(&connection->pending);
                if (!callback)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                r = varlink_message_unpack_reply(message, &error, &parameters, &flags);
                if (r < 0)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                if ((flags & VARLINK_REPLY_CONTINUES) && !(callback->call_flags & VARLINK_CALL_MORE))
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                r = callback->func(connection, error, parameters, flags, callback->userdata);

                if (!(flags & VARLINK_REPLY_CONTINUES)) {
                        STAILQ_REMOVE_HEAD(&connection->pending, entry);
                        free(callback);
                }

                if (r < 0)
                        break;

                if (!connection->stream)
                        break;
        }

        return r;
}

_public_ int varlink_connection_get_events(VarlinkConnection *connection) {
        return connection->events;
}

_public_ long varlink_connection_close(VarlinkConnection *connection) {
        connection->stream = varlink_stream_free(connection->stream);

        if (connection->closed_callback)
                connection->closed_callback(connection, connection->closed_userdata);

        return 0;
}

_public_ bool varlink_connection_is_closed(VarlinkConnection *connection) {
        return connection->stream == NULL;
}

_public_ int varlink_connection_get_fd(VarlinkConnection *connection) {
        if (!connection->stream)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        return connection->stream->fd;
}

_public_ long varlink_connection_call(VarlinkConnection *connection,
                                      const char *qualified_method,
                                      VarlinkObject *parameters,
                                      uint64_t flags,
                                      VarlinkReplyFunc func,
                                      void *userdata) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;
        ReplyCallback *callback;
        long r;

        if (!connection->stream)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        if (flags & VARLINK_CALL_MORE && flags & VARLINK_CALL_ONEWAY)
                return -VARLINK_ERROR_INVALID_CALL;

        r = varlink_message_pack_call(qualified_method, parameters, flags, &call);
        if (r < 0)
                return r;

        if (!(flags & VARLINK_CALL_ONEWAY)) {
                callback = calloc(1, sizeof(ReplyCallback));
                callback->call_flags = flags;
                callback->func = func;
                callback->userdata = userdata;
                STAILQ_INSERT_TAIL(&connection->pending, callback, entry);
        }

        r = varlink_stream_write(connection->stream, call);
        if (r < 0)
                return r;

        if (r == 0)
                connection->events |= EPOLLOUT;

        return 0;
}

_public_ void varlink_connection_set_closed_callback(VarlinkConnection *connection,
                                                     VarlinkConnectionClosedFunc closed,
                                                     void *userdata) {
        connection->closed_callback = closed;
        connection->closed_userdata = userdata;
}
