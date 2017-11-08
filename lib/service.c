#include "service.h"

#include "interface.h"
#include "message.h"
#include "object.h"
#include "stream.h"
#include "transport.h"
#include "uri.h"
#include "util.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "org.varlink.service.varlink.c.inc"

typedef struct ServiceConnection ServiceConnection;

struct VarlinkService {
        char *vendor;
        char *product;
        char *version;
        char *url;

        VarlinkURI *uri;
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
        VarlinkStream *stream;
        int events;
        VarlinkCall *call;
};

static long varlink_call_new(VarlinkCall **callp,
                             VarlinkService *service,
                             ServiceConnection *connection,
                             VarlinkObject *message) {
        _cleanup_(varlink_call_unrefp) VarlinkCall *call = NULL;
        long r;

        call = calloc(1, sizeof(VarlinkCall));
        call->refcount = 1;
        call->service = service;
        call->connection = connection;

        r = varlink_message_unpack_call(message, &call->method, &call->parameters, &call->flags);
        if (r < 0)
                return r;

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

        return fd - connection->stream->fd;
}

static ServiceConnection *service_connection_free(ServiceConnection *connection) {
        if (connection->call) {
                VarlinkCall *call = connection->call;

                if (call->canceled_callback)
                        call->canceled_callback(call, call->canceled_callback_data);

                varlink_call_unref(call);
        }

        if (connection->stream)
                varlink_stream_free(connection->stream);

        free(connection);
        return NULL;
}

static void service_connection_freep(ServiceConnection **connectionp) {
        if (*connectionp)
                service_connection_free(*connectionp);
}

static long service_connection_close(VarlinkService *service,
                                     ServiceConnection *connection) {
        if (connection->stream) {
                epoll_ctl(service->epoll_fd, EPOLL_CTL_DEL, connection->stream->fd, NULL);
                avl_tree_remove(service->connections, (void *)(unsigned long)connection->stream->fd);
        }

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

static long varlink_call_reply_interface_not_found(VarlinkCall *call, const char *interface) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "interface", interface);

        return varlink_call_reply_error(call, "org.varlink.service.InterfaceNotFound", parameters);
}

static long varlink_call_reply_method_not_found(VarlinkCall *call, const char *method) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "method", method);

        return varlink_call_reply_error(call, "org.varlink.service.MethodNotFound", parameters);
}

static long varlink_call_reply_method_not_implemented(VarlinkCall *call, const char *method) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;

        varlink_object_new(&parameters);
        varlink_object_set_string(parameters, "method", method);

        return varlink_call_reply_error(call, "org.varlink.service.MethodNotImplemented", parameters);
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
                return varlink_call_reply_interface_not_found(call, name);

        r = varlink_interface_write_description(interface, &string, -1,
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
        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
        VarlinkInterface *interface;
        VarlinkMethod *method;
        long r;

        r = varlink_uri_new(&uri, call->method, true);
        if (r < 0 || !uri->member)
                return varlink_call_reply_invalid_parameter(call, call->method);

        interface = avl_tree_find(service->interfaces, uri->interface);
        if (!interface)
                return varlink_call_reply_interface_not_found(call, uri->interface);

        method = varlink_interface_get_method(interface, uri->member);
        if (!method)
                return varlink_call_reply_method_not_found(call, uri->member);

        if (!method->callback)
                return varlink_call_reply_method_not_implemented(call, uri->member);

        return method->callback(service, call, call->parameters, call->flags, method->callback_userdata);
}

_public_ long varlink_service_new_raw(VarlinkService **servicep,
                                      const char *address,
                                      int listen_fd,
                                      VarlinkMethodCallback callback,
                                      void *userdata) {
        _cleanup_(varlink_service_freep) VarlinkService *service = NULL;
        long r;

        service = calloc(1, sizeof(VarlinkService));
        service->listen_fd = -1;
        service->epoll_fd = -1;

        r = varlink_uri_new(&service->uri, address, false);
        if (r < 0)
                return r;

        service->method_callback = callback;
        service->method_callback_userdata = userdata;

        avl_tree_new(&service->connections, connection_compare, (AVLFreeFunc)service_connection_free);

        if (listen_fd < 0) {
                _cleanup_(freep) char *path = NULL;

                listen_fd = varlink_listen(address, &path);
                if (listen_fd < 0)
                        return listen_fd;

                if (path && path[0] != '@') {
                        service->path_to_unlink = path;
                        path = NULL;
                }
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

        if (vendor) {
                service->vendor = strdup(vendor);
                if (!service->vendor)
                        return -VARLINK_ERROR_PANIC;
        }

        if (product) {
                service->product = strdup(product);
                if (!service->product)
                        return -VARLINK_ERROR_PANIC;
        }

        if (version) {
                service->version = strdup(version);
                if (!service->version)
                        return -VARLINK_ERROR_PANIC;
        }

        if (url) {
                service->url = strdup(url);
                if (!service->url)
                        return -VARLINK_ERROR_PANIC;
        }

        r = avl_tree_new(&service->interfaces, interface_compare, (AVLFreeFunc)varlink_interface_free);
        if (r < 0)
                return r;

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
        varlink_uri_free(service->uri);
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

        switch (avl_tree_insert(service->interfaces, interface->name, interface)) {
                case 0:
                        break;

                case -AVL_ERROR_KEY_EXISTS:
                        return -VARLINK_ERROR_INVALID_INTERFACE;

                default:
                        return -VARLINK_ERROR_PANIC;
        }

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

        r = varlink_transport_accept(service->uri, service->listen_fd);
        if (r < 0)
                return r; /* CannotAccept */

        varlink_stream_new(&connection->stream, (int)r, (pid_t)-1);

        r = epoll_add(service->epoll_fd, connection->stream->fd, EPOLLIN, connection);
        if (r < 0)
                return -VARLINK_ERROR_PANIC;

        avl_tree_insert(service->connections, (void *)(unsigned long)connection->stream->fd, connection);
        connection = NULL;

        return 0;
}

static long varlink_service_dispatch_connection(VarlinkService *service,
                                                ServiceConnection *connection,
                                                int events) {
        long r;

        connection->events = 0;

        if (events & EPOLLOUT) {
                r = varlink_stream_flush(connection->stream);
                if (r < 0)
                        return r;

                if (r > 0)
                        connection->events |= EPOLLOUT;
        }

        while (!connection->call) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;

                r = varlink_stream_read(connection->stream, &message);
                if (r < 0)
                        return service_connection_close(service, connection);

                if (r == 0)
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

        if (!connection->stream->hup)
                connection->events |= EPOLLIN;

        if (connection->events == 0)
                return service_connection_close(service, connection);

        if (epoll_mod(service->epoll_fd, connection->stream->fd, connection->events, connection) < 0)
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
                        switch (r) {
                                case -VARLINK_ERROR_ACCESS_DENIED:
                                        break;

                                default:
                                        return r;
                        }
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

        if (call->flags & VARLINK_CALL_ONEWAY && flags & VARLINK_REPLY_CONTINUES)
                return -VARLINK_ERROR_INVALID_CALL;

        if (call->flags & VARLINK_CALL_ONEWAY) {
                call->connection->call = varlink_call_unref(call);
                return 0;
        }

        r = varlink_message_pack_reply(NULL, parameters, flags, &message);
        if (r < 0)
                return r;

        r = varlink_stream_write(call->connection->stream, message);
        if (r < 0)
                return r;

        if (r == 0 && !(call->connection->events & EPOLLOUT)) {
                call->connection->events |= EPOLLOUT;

                if (epoll_mod(call->service->epoll_fd,
                              call->connection->stream->fd,
                              call->connection->events,
                              call->connection) < 0)
                        return -VARLINK_ERROR_PANIC;
        }

        if (flags & VARLINK_REPLY_CONTINUES)
                return 0;

        call->connection->call = varlink_call_unref(call);

        return 0;
}

_public_ long varlink_call_reply_error(VarlinkCall *call,
                                       const char *error,
                                       VarlinkObject *parameters) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *message = NULL;
        _cleanup_(varlink_uri_freep) VarlinkURI *uri_error = NULL;
        _cleanup_(varlink_uri_freep) VarlinkURI *uri_method = NULL;
        VarlinkInterface *interface;
        VarlinkInterfaceMember *member;
        _cleanup_(freep) char *string = NULL;
        long r;

        if (call != call->connection->call)
                return -VARLINK_ERROR_INVALID_CALL;

        r = varlink_uri_new(&uri_error, error, true);
        if (r < 0)
                return r;

        if (!uri_error->member)
                return -VARLINK_ERROR_INVALID_IDENTIFIER;

        interface = avl_tree_find(call->service->interfaces, uri_error->interface);
        if (!interface)
                return -VARLINK_ERROR_INVALID_IDENTIFIER;

        member = avl_tree_find(interface->member_tree, uri_error->member);
        if (!member || member->type != VARLINK_MEMBER_ERROR)
                return -VARLINK_ERROR_INVALID_IDENTIFIER;

        r = varlink_uri_new(&uri_method, call->method, true);
        if (r < 0)
                return r;

        if (strcmp(uri_error->interface, "org.varlink.service") != 0 &&
            strcmp(uri_error->interface, uri_method->interface) != 0)
                return -VARLINK_ERROR_INVALID_IDENTIFIER;

        r = varlink_message_pack_reply(error, parameters, 0, &message);
        if (r < 0)
                return r;

        r = varlink_stream_write(call->connection->stream, message);
        if (r < 0)
                return r;

        if (r == 0 && !(call->connection->events & EPOLLOUT)) {
                call->connection->events |= EPOLLOUT;

                if (epoll_mod(call->service->epoll_fd,
                              call->connection->stream->fd,
                              call->connection->events,
                              call->connection) < 0)
                        return -VARLINK_ERROR_PANIC;
        }

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

        return varlink_call_reply_error(call, "org.varlink.service.InvalidParameter", parameters);
}

VarlinkInterface *varlink_service_get_interface_by_name(VarlinkService *service, const char *name) {
        return avl_tree_find(service->interfaces, name);
}
