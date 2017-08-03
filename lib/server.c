#include "address.h"
#include "avltree.h"
#include "interface.h"
#include "object.h"
#include "server.h"
#include "service.h"
#include "socket.h"
#include "util.h"

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/epoll.h>

typedef struct ServerConnection ServerConnection;

struct VarlinkServer {
        char *address;

        VarlinkService *service;

        int listen_fd;
        bool listen_fd_cleanup;
        int epoll_fd;

        AVLTree *connections;
};

struct VarlinkCall {
        long refcount;

        VarlinkServer *server;
        ServerConnection *connection;
        VarlinkMethod *method;

        VarlinkCallCanceled canceled_callback;
        void *canceled_callback_data;
};

struct ServerConnection {
        VarlinkSocket socket;

        VarlinkObject *credentials;

        VarlinkCall *call;
};

static long varlink_call_new(VarlinkCall **callp,
                             VarlinkServer *server,
                             ServerConnection *connection) {
        VarlinkCall *call;

        call = calloc(1, sizeof(VarlinkCall));
        call->server = server;
        call->refcount = 1;
        call->connection = connection;

        *callp = call;

        return 0;
}

_public_ VarlinkCall *varlink_call_ref(VarlinkCall *call) {
        call->refcount += 1;

        return call;
}

_public_ VarlinkCall *varlink_call_unref(VarlinkCall *call) {
        call->refcount -= 1;

        if (call->refcount == 0)
                free(call);

        return NULL;
}

_public_ void varlink_call_unrefp(VarlinkCall **callp) {
        if (*callp)
                varlink_call_unref(*callp);
}

static long connection_compare(const void *key, void *value) {
        int fd = (int)(unsigned long)key;
        ServerConnection *connection = value;

        return fd - connection->socket.fd;
}

static ServerConnection *server_connection_free(ServerConnection *connection) {
        if (connection->call) {
                VarlinkCall *call = connection->call;

                if (call->canceled_callback)
                        call->canceled_callback(call, call->canceled_callback_data);

                varlink_call_unref(call);
        }

        varlink_socket_deinit(&connection->socket);

        if (connection->credentials)
                varlink_object_unref(connection->credentials);

        free(connection);

        return NULL;
}

static void server_connection_freep(ServerConnection **connectionp) {
        if (*connectionp)
                server_connection_free(*connectionp);
}

static long server_connection_close(VarlinkServer *server,
                                    ServerConnection *connection) {
        if (connection->socket.fd >= 0)
                epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, connection->socket.fd, NULL);

        avl_tree_remove(server->connections, (void *)(unsigned long)connection->socket.fd);

        return 0;
}

static long org_varlink_service_GetInfo(VarlinkServer *server,
                                        VarlinkCall *call,
                                        VarlinkObject *parameters,
                                        uint64_t flags,
                                        void *userdata) {
        _cleanup_(varlink_array_unrefp) VarlinkArray *interfaces = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        long r;

        r = varlink_array_new(&interfaces);
        if (r < 0)
                return r;

        for (AVLTreeNode *inode = avl_tree_first(server->service->interfaces); inode; inode = avl_tree_node_next(inode)) {
                VarlinkInterface *interface = avl_tree_node_get(inode);

                r = varlink_array_append_string(interfaces, interface->name);
                if (r < 0)
                        return r;
        }

        r = varlink_object_new(&info);
        if (r < 0)
                return r;

        if (varlink_object_set_string(info, "name", server->service->name) < 0 ||
            varlink_object_set_array(info, "interfaces", interfaces))
                return -VARLINK_ERROR_PANIC;

        if (server->service->properties &&
            varlink_object_set_object(info, "properties", server->service->properties) < 0)
                return -VARLINK_ERROR_PANIC;

        return varlink_call_reply(call, info, 0);
}

static long org_varlink_service_GetInterface(VarlinkServer *server,
                                             VarlinkCall *call,
                                             VarlinkObject *parameters,
                                             uint64_t flags,
                                             void *userdata) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        const char *name;
        VarlinkInterface *interface;
        _cleanup_(freep) char *string = NULL;
        long r;

        if (varlink_object_get_string(parameters, "name", &name) < 0)
                return varlink_call_reply_invalid_parameters(call, "name", NULL);

        interface = varlink_service_get_interface_by_name(server->service, name);
        if (!interface)
                return varlink_call_reply_error(call, "org.varlink.service.InterfaceNotFound", NULL);

        r = varlink_interface_write_interfacestring(interface, &string, -1, -1,
                                                    NULL, NULL, NULL, NULL,
                                                    NULL, NULL, NULL, NULL);
        if (r < 0)
                return r;

        varlink_object_new(&out);
        varlink_object_set_string(out, "interfacestring", string);

        return varlink_call_reply(call, out, 0);
}

_public_ long varlink_server_new(VarlinkServer **serverp,
                                 const char *address,
                                 int listen_fd,
                                 const char *name,
                                 VarlinkObject *properties,
                                 const char **interfacestrings,
                                 unsigned long n_interfaces) {
        _cleanup_(varlink_server_freep) VarlinkServer *server = NULL;
        const char *parameter;
        long r;

        server = calloc(1, sizeof(VarlinkServer));
        server->address = strdup(address);
        server->listen_fd = -1;
        server->epoll_fd = -1;

        if (listen_fd < 0) {
                switch (varlink_address_get_type(address, &parameter)) {
                        case VARLINK_ADDRESS_UNIX:
                                r = varlink_socket_listen_unix(parameter, &server->listen_fd);
                                break;
                        case VARLINK_ADDRESS_TCP:
                                r = varlink_socket_listen_tcp(parameter, &server->listen_fd);
                                break;
                        default:
                                return -VARLINK_ERROR_INVALID_ADDRESS;
                }

                /* CannotListen or InvalidAddress */
                if (r < 0)
                        return r;

                server->listen_fd_cleanup = true;
        } else {
                server->listen_fd = listen_fd;
        }

        /* An activator only sets up the listen_fd only */
        if (interfacestrings) {
                if (!varlink_interface_name_valid(name))
                        return -VARLINK_ERROR_INVALID_INTERFACE;

                avl_tree_new(&server->connections, connection_compare, (AVLFreeFunc)server_connection_free);

                server->epoll_fd = epoll_create1(EPOLL_CLOEXEC);

                if (epoll_add(server->epoll_fd, server->listen_fd, EPOLLIN, server) < 0)
                        return -VARLINK_ERROR_PANIC;

                r = varlink_service_new(&server->service, name, properties);
                if (r < 0)
                        return r;

                r = varlink_server_set_method_callback(server,
                                                       "org.varlink.service.GetInfo",
                                                       org_varlink_service_GetInfo, NULL);
                if (r < 0)
                        return -VARLINK_ERROR_PANIC;

                r = varlink_server_set_method_callback(server,
                                                       "org.varlink.service.GetInterface",
                                                       org_varlink_service_GetInterface, NULL);
                if (r < 0)
                        return -VARLINK_ERROR_PANIC;

                for (unsigned long i = 0; i < n_interfaces; i += 1) {
                        VarlinkInterface *interface;

                        r = varlink_interface_new(&interface, interfacestrings[i], NULL);
                        if (r < 0)
                                return r;

                        r = varlink_service_add_interface(server->service, interface);
                        if (r < 0)
                                return r;
                }
        }

        *serverp = server;
        server = NULL;

        return 0;
}

_public_ VarlinkServer *varlink_server_free(VarlinkServer *server) {
        if (server->epoll_fd >= 0)
                close(server->epoll_fd);

        if (server->listen_fd >= 0) {
                const char *parameter;

                if (server->listen_fd_cleanup &&
                    varlink_address_get_type(server->address, &parameter) == VARLINK_ADDRESS_UNIX)
                        /* ignore errors - continue freeing the server */
                        varlink_socket_cleanup_unix(parameter);

                close(server->listen_fd);
        }

        if (server->connections)
                avl_tree_free(server->connections);

        if (server->service)
                varlink_service_free(server->service);

        free(server->address);
        free(server);

        return NULL;
}

_public_ void varlink_server_freep(VarlinkServer **serverp) {
        if (*serverp)
                varlink_server_free(*serverp);
}

_public_ int varlink_server_get_fd(VarlinkServer *server) {
        return server->epoll_fd;
}

_public_ int varlink_server_get_listen_fd(VarlinkServer *server) {
        return server->listen_fd;
}

static long varlink_server_accept(VarlinkServer *server) {
        _cleanup_(server_connection_freep) ServerConnection *connection = NULL;
        long r;

        connection = calloc(1, sizeof(ServerConnection));
        varlink_socket_init(&connection->socket);

        switch (varlink_address_get_type(server->address, NULL)) {
                case VARLINK_ADDRESS_UNIX:
                        r = varlink_socket_accept_unix(server->listen_fd,
                                                       &connection->socket,
                                                       &connection->credentials);
                        break;
                case VARLINK_ADDRESS_TCP:
                        r = varlink_socket_accept_tcp(server->listen_fd,
                                                      &connection->socket,
                                                      &connection->credentials);
                        break;
                default:
                        return -VARLINK_ERROR_PANIC;
        }

        /* CannotAccept */
        if (r < 0)
                return r;

        r = epoll_add(server->epoll_fd, connection->socket.fd, EPOLLIN, connection);
        if (r < 0)
                return -VARLINK_ERROR_PANIC;

        avl_tree_insert(server->connections, (void *)(unsigned long)connection->socket.fd, connection);
        connection = NULL;

        return 0;
}

static long varlink_server_handle_call(VarlinkServer *server,
                                       ServerConnection *connection,
                                       VarlinkObject *message) {
        const char *qualified_method = NULL;
        _cleanup_(freep) char *method_name = NULL;
        _cleanup_(freep) char *interface_name = NULL;
        VarlinkObject *parameters = NULL;
        VarlinkInterface *interface;
        bool more;
        uint64_t flags = 0;
        long r;

        r = varlink_call_new(&connection->call, server, connection);
        if (r < 0)
                return r;

        if (varlink_object_get_string(message, "method", &qualified_method) < 0 ||
            varlink_interface_parse_qualified_name(qualified_method, &interface_name, &method_name) < 0)
                return varlink_call_reply_invalid_parameters(connection->call, "method", NULL);

        r = varlink_object_get_object(message, "parameters", &parameters);
        if (r < 0)
                return varlink_call_reply_invalid_parameters(connection->call, "parameters", NULL);

        if (varlink_object_get_bool(message, "more", &more) == 0 && more)
                flags |= VARLINK_CALL_MORE;

        interface = varlink_service_get_interface_by_name(server->service, interface_name);
        if (!interface)
                return varlink_call_reply_error(connection->call, "org.varlink.service.InterfaceNotFound", NULL);

        connection->call->method = varlink_interface_get_method(interface, method_name);
        if (!connection->call->method)
                return varlink_call_reply_error(connection->call, "org.varlink.service.MethodNotFound", NULL);

        if (!connection->call->method->server_callback)
                return varlink_call_reply_error(connection->call, "org.varlink.service.MethodNotImplemented", NULL);

        r = connection->call->method->server_callback(server, connection->call, parameters, flags,
                                                      connection->call->method->server_callback_userdata);
        if (r < 0)
                return server_connection_close(server, connection);

        return 0;
}

static long varlink_server_dispatch_connection(VarlinkServer *server,
                                               ServerConnection *connection,
                                               int events) {
        long r;

        r = varlink_socket_dispatch(&connection->socket, events);
        if (r < 0)
                return r;

        for (;;) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;

                r = varlink_socket_read(&connection->socket, &message);
                if (r < 0)
                        return server_connection_close(server, connection);

                if (!message)
                        break;

                r = varlink_server_handle_call(server, connection, message);
                if (r < 0)
                        return server_connection_close(server, connection);
        }

        if (epoll_mod(server->epoll_fd,
                      connection->socket.fd,
                      varlink_socket_get_events(&connection->socket),
                      connection) < 0)
                return -VARLINK_ERROR_PANIC;

        return 0;
}

_public_ long varlink_server_process_events(VarlinkServer *server) {
        for(;;) {
                int n;
                struct epoll_event ev;
                long r;

                n = epoll_wait(server->epoll_fd, &ev, 1, 0);
                if (n < 0)
                        return -VARLINK_ERROR_PANIC;

                if (n == 0)
                        return 0;

                if (ev.data.ptr == server) {
                        if ((ev.events & EPOLLIN) == 0)
                                return -VARLINK_ERROR_PANIC;

                        r = varlink_server_accept(server);
                        if (r < 0)
                                return r;
                } else {
                        ServerConnection *connection = ev.data.ptr;

                        r = varlink_server_dispatch_connection(server, connection, ev.events);
                        if (r < 0)
                                return r;
                }
        }

        return 0;
}

long varlink_server_get_interface_by_name(VarlinkServer *server,
                                          VarlinkInterface **interfacep,
                                          const char *name) {
        VarlinkInterface *interface;

        interface = varlink_service_get_interface_by_name(server->service, name);
        if (!interface)
                return -VARLINK_ERROR_INTERFACE_NOT_FOUND;

        *interfacep = interface;

        return 0;
}

_public_ long varlink_server_set_method_callback(VarlinkServer *server,
                                                 const char *qualified_method,
                                                 VarlinkMethodServerCallback callback,
                                                 void *callback_userdata) {
        _cleanup_(freep) char *interface_name = NULL;
        _cleanup_(freep) char *method_name = NULL;
        VarlinkInterface *interface;
        VarlinkMethod *method;
        long r;

        r = varlink_interface_parse_qualified_name(qualified_method, &interface_name, &method_name);
        if (r < 0)
                return r;

        interface = varlink_service_get_interface_by_name(server->service, interface_name);
        if (!interface)
                return -VARLINK_ERROR_INTERFACE_NOT_FOUND;

        method = varlink_interface_get_method(interface, method_name);
        if (!method)
                return r;

        method->server_callback = callback;
        method->server_callback_userdata = callback_userdata;

        return 0;
}

_public_ long varlink_call_set_canceled_callback(VarlinkCall *call,
                                                 VarlinkCallCanceled callback,
                                                 void *userdata) {
        call->canceled_callback = callback;
        call->canceled_callback_data = userdata;

        return 0;
}

_public_ long varlink_call_reply(VarlinkCall *call,
                                 VarlinkObject *parameters,
                                 uint64_t flags) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
        long r;

        if (call != call->connection->call)
                return -VARLINK_ERROR_INVALID_CALL;

        r = varlink_object_new(&message);
        if (r < 0)
                return r;

        if (parameters)
                varlink_object_set_object(message, "parameters", parameters);
        else
                varlink_object_set_empty_object(message, "parameters");

        if (flags & VARLINK_REPLY_CONTINUES)
                varlink_object_set_bool(message, "continues", true);

        r = varlink_socket_write(&call->connection->socket, message);
        if (r < 0)
                return r;

        if (epoll_mod(call->server->epoll_fd,
                      call->connection->socket.fd,
                      EPOLLOUT,
                      call->connection) < 0)
                return -VARLINK_ERROR_PANIC;

        if (flags & VARLINK_REPLY_CONTINUES)
                return 0;

        call->connection->call = varlink_call_unref(call);

        return 0;
}

_public_ long varlink_call_reply_error(VarlinkCall *call,
                                       const char *error,
                                       VarlinkObject *parameters) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
        long r;

        if (call != call->connection->call)
                return -VARLINK_ERROR_INVALID_CALL;

        r = varlink_object_new(&message);
        if (r < 0)
                return r;

        varlink_object_set_string(message, "error", error);

        if (parameters)
                varlink_object_set_object(message, "parameters", parameters);
        else
                varlink_object_set_empty_object(message, "parameters");

        r = varlink_socket_write(&call->connection->socket, message);
        if (r < 0)
                return r;

        call->connection->call = varlink_call_unref(call);

        return 0;
}

_public_ long varlink_call_reply_invalid_parameters(VarlinkCall *call, ...) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        _cleanup_(varlink_array_unrefp) VarlinkArray *names = NULL;
        va_list ap;
        long r;

        r = varlink_array_new(&names);
        if (r < 0)
                return r;

        va_start(ap, call);
        for (;;) {
                const char *name = va_arg(ap, const char *);

                if (!name)
                        break;

                varlink_array_append_string(names, name);
        }
        va_end(ap);

        r = varlink_object_new(&parameters);
        if (r < 0)
                return r;

        varlink_object_set_array(parameters, "names", names);

        return varlink_call_reply_error(call, "org.varlink.service.InvalidParameters", parameters);
}
