#include "service.h"

#include "interface.h"
#include "object.h"
#include "socket.h"
#include "util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "org.varlink.service.varlink.inc.c"

typedef struct ServiceConnection ServiceConnection;

struct VarlinkService {
        char *vendor;
        char *product;
        char *version;
        char *url;
        char *address;
        AVLTree *interfaces;

        int listen_fd;
        char *path_to_unlink;
        int epoll_fd;

        AVLTree *connections;
        VarlinkMethodCallback method_callback;
        void *method_callback_userdata;
};

struct VarlinkCall {
        long refcount;

        VarlinkService *service;
        ServiceConnection *connection;

        char *method;
        VarlinkObject *parameters;
        uint64_t flags;

        VarlinkCallCanceled canceled_callback;
        void *canceled_callback_data;
};

struct ServiceConnection {
        VarlinkSocket socket;
        VarlinkObject *credentials;
        VarlinkCall *call;
};

static long varlink_call_new(VarlinkCall **callp,
                             VarlinkService *service,
                             ServiceConnection *connection,
                             VarlinkObject *message) {
        _cleanup_(varlink_call_unrefp) VarlinkCall *call = NULL;
        const char *method;
        VarlinkObject *parameters;
        bool more;
        long r;

        call = calloc(1, sizeof(VarlinkCall));
        call->refcount = 1;
        call->service = service;
        call->connection = connection;

        r = varlink_object_get_string(message, "method", &method);
        if (r < 0)
                return -VARLINK_ERROR_INVALID_METHOD;

        call->method = strdup(method);

        r = varlink_object_get_object(message, "parameters", &parameters);
        if (parameters)
                call->parameters = varlink_object_ref(parameters);
        else
                varlink_object_new(&call->parameters);

        if (varlink_object_get_bool(message, "more", &more) == 0 && more)
                call->flags |= VARLINK_CALL_MORE;

        *callp = call;
        call = NULL;

        return 0;
}

_public_ VarlinkCall *varlink_call_ref(VarlinkCall *call) {
        call->refcount += 1;

        return call;
}

_public_ VarlinkCall *varlink_call_unref(VarlinkCall *call) {
        call->refcount -= 1;

        if (call->refcount == 0) {
                if (call->parameters)
                        varlink_object_unref(call->parameters);

                free(call->method);
                free(call);
        }

        return NULL;
}

_public_ void varlink_call_unrefp(VarlinkCall **callp) {
        if (*callp)
                varlink_call_unref(*callp);
}

_public_ const char *varlink_call_get_method(VarlinkCall *call) {
        return call->method;
}

static long interface_compare(const void *key, void *value) {
        VarlinkInterface *interface = value;

        return strcmp(key, interface->name);
}

static long connection_compare(const void *key, void *value) {
        int fd = (int)(unsigned long)key;
        ServiceConnection *connection = value;

        return fd - connection->socket.fd;
}

static ServiceConnection *service_connection_free(ServiceConnection *connection) {
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

static void service_connection_freep(ServiceConnection **connectionp) {
        if (*connectionp)
                service_connection_free(*connectionp);
}

static long service_connection_close(VarlinkService *service,
                                     ServiceConnection *connection) {
        if (connection->socket.fd >= 0)
                epoll_ctl(service->epoll_fd, EPOLL_CTL_DEL, connection->socket.fd, NULL);

        avl_tree_remove(service->connections, (void *)(unsigned long)connection->socket.fd);

        return 0;
}

static long org_varlink_service_GetInfo(VarlinkService *service,
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

        for (AVLTreeNode *inode = avl_tree_first(service->interfaces); inode; inode = avl_tree_node_next(inode)) {
                VarlinkInterface *interface = avl_tree_node_get(inode);

                r = varlink_array_append_string(interfaces, interface->name);
                if (r < 0)
                        return r;
        }

        r = varlink_object_new(&info);
        if (r < 0)
                return r;

        if (service->vendor)
                varlink_object_set_string(info, "vendor", service->vendor);
        if (service->product)
                varlink_object_set_string(info, "product", service->product);
        if (service->version)
                varlink_object_set_string(info, "version", service->version);
        if (service->url)
                varlink_object_set_string(info, "url", service->url);

        varlink_object_set_array(info, "interfaces", interfaces);

        return varlink_call_reply(call, info, 0);
}

static long org_varlink_service_GetInterfaceDescription(VarlinkService *service,
                                                        VarlinkCall *call,
                                                        VarlinkObject *parameters,
                                                        uint64_t flags,
                                                        void *userdata) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        const char *name;
        VarlinkInterface *interface;
        _cleanup_(freep) char *string = NULL;
        long r;

        if (varlink_object_get_string(parameters, "interface", &name) < 0)
                return varlink_call_reply_invalid_parameter(call, "interface");

        interface = avl_tree_find(service->interfaces, name);
        if (!interface)
                return varlink_call_reply_error(call, "org.varlink.service.InterfaceNotFound", NULL);

        r = varlink_interface_write_interfacestring(interface, &string, -1, -1,
                                                    NULL, NULL, NULL, NULL,
                                                    NULL, NULL, NULL, NULL);
        if (r < 0)
                return r;

        varlink_object_new(&out);
        varlink_object_set_string(out, "description", string);

        return varlink_call_reply(call, out, 0);
}

static long varlink_service_method_callback(VarlinkService *service,
                                            VarlinkCall *call,
                                            VarlinkObject *parameters,
                                            uint64_t flags,
                                            void *userdata) {
        _cleanup_(freep) char *interface_name;
        _cleanup_(freep) char *method_name;
        VarlinkInterface *interface;
        VarlinkMethod *method;
        long r;

        r = varlink_interface_parse_qualified_name(call->method, &interface_name, &method_name);
        if (r < 0)
                return varlink_call_reply_error(call, "org.varlink.service.MethodNotFound", NULL);

        interface = avl_tree_find(service->interfaces, interface_name);
        if (!interface)
                return varlink_call_reply_error(call, "org.varlink.service.InterfaceNotFound", NULL);

        method = varlink_interface_get_method(interface, method_name);
        if (!method)
                return varlink_call_reply_error(call, "org.varlink.service.MethodNotFound", NULL);

        if (!method->callback)
                return varlink_call_reply_error(call, "org.varlink.service.MethodNotImplemented", NULL);

        return method->callback(service, call, call->parameters, call->flags, method->callback_userdata);
}

_public_ long varlink_service_new(VarlinkService **servicep,
                                  const char *vendor,
                                  const char *product,
                                  const char *version,
                                  const char *url,
                                  const char *address,
                                  int listen_fd) {
        _cleanup_(varlink_service_freep) VarlinkService *service = NULL;
        long r;

        r = varlink_service_new_raw(&service, address, listen_fd, varlink_service_method_callback, NULL);
        if (r < 0)
                return r;

        if (vendor)
                service->vendor = strdup(vendor);
        if (product)
                service->product = strdup(product);
        if (version)
                service->version = strdup(version);
        if (url)
                service->url = strdup(url);

        avl_tree_new(&service->interfaces, interface_compare, (AVLFreeFunc)varlink_interface_free);

        r = varlink_service_add_interface(service, org_varlink_service_varlink,
                                          "GetInfo", org_varlink_service_GetInfo, NULL,
                                          "GetInterfaceDescription", org_varlink_service_GetInterfaceDescription, NULL,
                                          NULL);
        if (r < 0)
                return r;

        *servicep = service;
        service = NULL;

        return 0;
}

_public_ long varlink_service_new_raw(VarlinkService **servicep,
                                      const char *address,
                                      int listen_fd,
                                      VarlinkMethodCallback callback,
                                      void *userdata) {
        _cleanup_(varlink_service_freep) VarlinkService *service = NULL;

        service = calloc(1, sizeof(VarlinkService));
        service->listen_fd = -1;
        service->epoll_fd = -1;

        service->address = strdup(address);
        service->method_callback = callback;
        service->method_callback_userdata = userdata;

        avl_tree_new(&service->connections, connection_compare, (AVLFreeFunc)service_connection_free);

        if (listen_fd < 0) {
                listen_fd = varlink_listen(address, &service->path_to_unlink);
                if (listen_fd < 0)
                        return listen_fd;
        }

        service->listen_fd = listen_fd;

        service->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (service->epoll_fd < 0)
                return -VARLINK_ERROR_PANIC;

        if (epoll_add(service->epoll_fd, service->listen_fd, EPOLLIN, service) < 0)
                return -VARLINK_ERROR_PANIC;

        *servicep = service;
        service = NULL;

        return 0;
}

_public_ VarlinkService *varlink_service_free(VarlinkService *service) {
        if (service->epoll_fd >= 0)
                close(service->epoll_fd);

        if (service->listen_fd >= 0)
                close(service->listen_fd);

        if (service->path_to_unlink) {
                unlink(service->path_to_unlink);
                free(service->path_to_unlink);
        }

        if (service->connections)
                avl_tree_free(service->connections);

        if (service->interfaces)
                avl_tree_free(service->interfaces);

        free(service->vendor);
        free(service->product);
        free(service->version);
        free(service->url);

        free(service->address);
        free(service);

        return NULL;
}

_public_ void varlink_service_freep(VarlinkService **servicep) {
        if (*servicep)
                varlink_service_free(*servicep);
}

_public_ long varlink_service_add_interface(VarlinkService *service,
                                            const char *interface_description,
                                            ...) {
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        va_list args;
        long r;

        if (!service->interfaces)
                return -VARLINK_ERROR_PANIC;

        r = varlink_interface_new(&interface, interface_description, NULL);
        if (r < 0)
                return r;

        va_start(args, interface_description);
        for (;;) {
                const char *name;
                VarlinkMethod *method;

                name = va_arg(args, const char *);
                if (!name)
                        break;

                method = varlink_interface_get_method(interface, name);
                if (!method)
                        return -VARLINK_ERROR_METHOD_NOT_FOUND;

                method->callback = va_arg(args, VarlinkMethodCallback);
                method->callback_userdata = va_arg(args, void *);
        }
        va_end(args);

        r = avl_tree_insert(service->interfaces, interface->name, interface);
        if (r < 0)
                return -VARLINK_ERROR_DUPLICATE_INTERFACE;

        interface = NULL;

        return 0;
}

_public_ int varlink_service_get_fd(VarlinkService *service) {
        return service->epoll_fd;
}

static long varlink_service_accept(VarlinkService *service) {
        _cleanup_(service_connection_freep) ServiceConnection *connection = NULL;
        long r;

        connection = calloc(1, sizeof(ServiceConnection));
        varlink_socket_init(&connection->socket);

        r = varlink_socket_accept(&connection->socket,
                                  service->address,
                                  service->listen_fd,
                                  &connection->credentials);
        if (r < 0)
                return r; /* CannotAccept */

        r = epoll_add(service->epoll_fd, connection->socket.fd, EPOLLIN, connection);
        if (r < 0)
                return -VARLINK_ERROR_PANIC;

        avl_tree_insert(service->connections, (void *)(unsigned long)connection->socket.fd, connection);
        connection = NULL;

        return 0;
}

static long varlink_service_dispatch_connection(VarlinkService *service,
                                                ServiceConnection *connection,
                                                int events) {
        long r;

        r = varlink_socket_dispatch(&connection->socket, events);
        if (r < 0)
                return r;

        /*
         * If we're in a call and the other side has lost interest,
         * close the connection.
         */
        if (connection->socket.hup && connection->call)
                return service_connection_close(service, connection);

        while (connection->call == NULL) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;

                r = varlink_socket_read(&connection->socket, &message);
                if (r < 0)
                        return service_connection_close(service, connection);

                if (!message)
                        break;

                r = varlink_call_new(&connection->call, service, connection, message);
                if (r < 0)
                        return r;

                r = service->method_callback(service,
                                             connection->call,
                                             connection->call->parameters,
                                             connection->call->flags,
                                             service->method_callback_userdata);
                if (r < 0)
                        return service_connection_close(service, connection);
        }

        if (epoll_mod(service->epoll_fd,
                      connection->socket.fd,
                      varlink_socket_get_events(&connection->socket),
                      connection) < 0)
                return -VARLINK_ERROR_PANIC;

        return 0;
}

_public_ long varlink_service_process_events(VarlinkService *service) {
        for(;;) {
                int n;
                struct epoll_event ev;
                long r;

                n = epoll_wait(service->epoll_fd, &ev, 1, 0);
                if (n < 0)
                        return -VARLINK_ERROR_PANIC;

                if (n == 0)
                        return 0;

                if (ev.data.ptr == service) {
                        if ((ev.events & EPOLLIN) == 0)
                                return -VARLINK_ERROR_PANIC;

                        r = varlink_service_accept(service);
                        if (r < 0)
                                return r;
                } else {
                        ServiceConnection *connection = ev.data.ptr;

                        r = varlink_service_dispatch_connection(service, connection, ev.events);
                        if (r < 0)
                                return r;
                }
        }

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

        if (epoll_mod(call->service->epoll_fd,
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

_public_ long varlink_call_reply_invalid_parameter(VarlinkCall *call, const char *parameter) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
        long r;

        r = varlink_object_new(&parameters);
        if (r < 0)
                return r;

        varlink_object_set_string(parameters, "parameter", parameter);

        return varlink_call_reply_error(call, "org.varlink.service.InvalidParameters", parameters);
}

VarlinkInterface *varlink_service_get_interface_by_name(VarlinkService *service, const char *name) {
        return avl_tree_find(service->interfaces, name);
}
