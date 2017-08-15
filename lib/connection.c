#include "object.h"
#include "socket.h"
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>

typedef struct ClosedCallback ClosedCallback;
typedef struct ReplyCallback ReplyCallback;

struct ClosedCallback {
        VarlinkConnectionClosedFunc func;
        void *userdata;
        ClosedCallback *next;
};

struct ReplyCallback {
        uint64_t call_flags;
        VarlinkReplyFunc func;
        void *userdata;

        STAILQ_ENTRY(ReplyCallback) entry;
};

struct VarlinkConnection {
        char *address;

        VarlinkSocket socket;

        STAILQ_HEAD(pending, ReplyCallback) pending;

        ClosedCallback *closed_callbacks;
};

_public_ long varlink_connection_new(VarlinkConnection **connectionp, const char *address) {
        _cleanup_(varlink_connection_freep) VarlinkConnection *connection = NULL;
        long r;

        connection = calloc(1, sizeof(VarlinkConnection));
        connection->address = strdup(address);
        varlink_socket_init(&connection->socket);
        STAILQ_INIT(&connection->pending);

        r = varlink_socket_connect(&connection->socket, address);
        if (r < 0)
                return r; /* CannotConnect or InvalidAddress */

        *connectionp = connection;
        connection = NULL;

        return 0;
}

_public_ VarlinkConnection *varlink_connection_free(VarlinkConnection *connection) {
        if (connection->socket.fd >= 0)
                varlink_connection_close(connection);

        while (!STAILQ_EMPTY(&connection->pending)) {
                _cleanup_(freep) ReplyCallback *cb = NULL;

                cb = STAILQ_FIRST(&connection->pending);
                STAILQ_REMOVE_HEAD(&connection->pending, entry);
        }

        free(connection->address);
        free(connection);

        return NULL;
}

_public_ void varlink_connection_freep(VarlinkConnection **connectionp) {
        if (*connectionp)
                varlink_connection_free(*connectionp);
}

_public_ long varlink_connection_process_events(VarlinkConnection *connection, int events) {
        long r;

        if (connection->socket.fd < 0)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        r = varlink_socket_dispatch(&connection->socket, events);
        if (r < 0)
                return r;

        /* Check if the socket is valid, because a callback might have closed the connection */
        while (connection->socket.fd >= 0) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
                VarlinkObject *parameters = NULL;
                const char *error = NULL;
                bool continues = false;
                long flags = 0;
                ReplyCallback *callback;

                r = varlink_socket_read(&connection->socket, &message);
                if (r < 0)
                        return r;

                if (!message)
                        return 0;

                callback = STAILQ_FIRST(&connection->pending);
                if (!callback)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                r = varlink_object_get_string(message, "error", &error);
                if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                        return -VARLINK_ERROR_INVALID_MESSAGE;

                r = varlink_object_get_object(message, "parameters", &parameters);
                if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                        return VARLINK_ERROR_INVALID_MESSAGE;

                r = varlink_object_get_bool(message, "continues", &continues);
                if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                        return VARLINK_ERROR_INVALID_MESSAGE;

                if (continues && !(callback->call_flags & VARLINK_CALL_MORE))
                        return VARLINK_ERROR_INVALID_MESSAGE;

                if (continues)
                        flags |= VARLINK_REPLY_CONTINUES;

                callback->func(connection, error, parameters, flags, callback->userdata);

                if (!continues) {
                        STAILQ_REMOVE_HEAD(&connection->pending, entry);
                        free(callback);
                }
        }

        return 0;
}

_public_ int varlink_connection_get_events(VarlinkConnection *connection) {
        return varlink_socket_get_events(&connection->socket);
}

_public_ long varlink_connection_close(VarlinkConnection *connection) {
        ClosedCallback *callback;

        varlink_socket_deinit(&connection->socket);

        callback = connection->closed_callbacks;
        while (callback) {
                ClosedCallback *previous;

                callback->func(connection, callback->userdata);

                previous = callback;
                callback = callback->next;
                free(previous);
        }

        connection->closed_callbacks = NULL;

        return 0;
}

_public_ bool varlink_connection_is_closed(VarlinkConnection *connection) {
        return connection->socket.fd < 0;
}

_public_ int varlink_connection_get_fd(VarlinkConnection *connection) {
        return connection->socket.fd;
}

_public_ long varlink_connection_call(VarlinkConnection *connection,
                                      const char *qualified_method,
                                      VarlinkObject *parameters,
                                      uint64_t flags,
                                      VarlinkReplyFunc func,
                                      void *userdata) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;
        ReplyCallback *callback;

        if (connection->socket.fd < 0)
                return -VARLINK_ERROR_CONNECTION_CLOSED;

        if (varlink_object_new(&call) < 0 ||
            varlink_object_set_string(call, "method", qualified_method) < 0)
                return -VARLINK_ERROR_PANIC;

        callback = calloc(1, sizeof(ReplyCallback));
        callback->call_flags = flags;
        callback->func = func;
        callback->userdata = userdata;
        STAILQ_INSERT_TAIL(&connection->pending, callback, entry);

        if (parameters)
                varlink_object_set_object(call, "parameters", parameters);
        else
                varlink_object_set_empty_object(call, "parameters");

        if (flags & VARLINK_CALL_MORE)
                varlink_object_set_bool(call, "more", true);

        return varlink_socket_write(&connection->socket, call);
}

_public_ void varlink_connection_set_close_callback(VarlinkConnection *connection,
                                                    VarlinkConnectionClosedFunc closed,
                                                    void *userdata) {
        ClosedCallback *callback;

        callback = calloc(1, sizeof(ClosedCallback));
        callback->func = closed;
        callback->userdata = userdata;

        callback->next = connection->closed_callbacks;
        connection->closed_callbacks = callback;
}
