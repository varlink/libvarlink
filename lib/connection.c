#include "address.h"
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
#include <unistd.h>

typedef struct ClosedCallback ClosedCallback;

struct ClosedCallback {
        VarlinkConnectionClosedFunc func;
        void *userdata;
        ClosedCallback *next;
};

struct VarlinkConnection {
        char *address;

        VarlinkSocket socket;

        ClosedCallback *closed_callbacks;
};

_public_ long varlink_connection_new(VarlinkConnection **connectionp, const char *address) {
        _cleanup_(varlink_connection_closep) VarlinkConnection *connection = NULL;
        long r;

        connection = calloc(1, sizeof(VarlinkConnection));
        connection->address = strdup(address);
        varlink_socket_init(&connection->socket);

        switch (varlink_address_get_type(address)) {
                case VARLINK_ADDRESS_UNIX:
                        r = varlink_socket_connect_unix(&connection->socket, address);
                        break;
                case VARLINK_ADDRESS_TCP:
                        r = varlink_socket_connect_tcp(&connection->socket, address);
                        break;
                default:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        /* CannotConnect or InvalidAddress */
        if (r < 0)
                return r;

        *connectionp = connection;
        connection = NULL;

        return 0;
}

_public_ long varlink_connection_process_events(VarlinkConnection *connection, int events) {
        return varlink_socket_dispatch(&connection->socket, events);
}

_public_ int varlink_connection_get_events(VarlinkConnection *connection) {
        return varlink_socket_get_events(&connection->socket);
}

_public_ VarlinkConnection *varlink_connection_close(VarlinkConnection *connection) {
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

        free(connection->address);
        free(connection);

        return NULL;
}

_public_ void varlink_connection_closep(VarlinkConnection **connectionp) {
        if (*connectionp)
                varlink_connection_close(*connectionp);
}

_public_ int varlink_connection_get_fd(VarlinkConnection *connection) {
        return connection->socket.fd;
}

_public_ long varlink_connection_receive_reply(VarlinkConnection *connection,
                                               VarlinkObject **parametersp,
                                               char **errorp,
                                               long *flagsp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
        VarlinkObject *parameters = NULL;
        const char *error = NULL;
        bool continues;
        long flags = 0;
        long r;

        if (errorp)
                *errorp = NULL;

        r = varlink_socket_read(&connection->socket, &message);
        if (r < 0)
                return r;

        if (!message) {
                *parametersp = NULL;
                return 0;
        }

        r = varlink_object_get_string(message, "error", &error);
        switch (r) {
                case 0:
                case -VARLINK_ERROR_UNKNOWN_FIELD:
                        break;
                default:
                        return -VARLINK_ERROR_INVALID_MESSAGE;
        }

        r = varlink_object_get_object(message, "parameters", &parameters);
        if (r < 0)
                return VARLINK_ERROR_INVALID_MESSAGE;

        if (varlink_object_get_bool(message, "continues", &continues) == 0 && continues)
                flags |= continues;

        if (parametersp)
                *parametersp = varlink_object_ref(parameters);

        if (errorp) {
                if (error)
                        *errorp = strdup(error);
                else
                        *errorp = NULL;
        }

        if (flagsp)
                *flagsp = flags;

        return 0;
}

_public_ long varlink_connection_call(VarlinkConnection *connection,
                                      const char *qualified_method,
                                      VarlinkObject *parameters,
                                      uint64_t flags) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;

        if (varlink_object_new(&call) < 0 ||
            varlink_object_set_string(call, "method", qualified_method) < 0)
                return -VARLINK_ERROR_PANIC;

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
