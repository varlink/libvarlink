#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
        VARLINK_ERROR_PANIC = 1,
        VARLINK_ERROR_INVALID_INTERFACE,
        VARLINK_ERROR_INVALID_ADDRESS,
        VARLINK_ERROR_INVALID_METHOD,
        VARLINK_ERROR_DUPLICATE_INTERFACE,
        VARLINK_ERROR_INTERFACE_NOT_FOUND,
        VARLINK_ERROR_METHOD_NOT_FOUND,
        VARLINK_ERROR_CANNOT_CONNECT,
        VARLINK_ERROR_CANNOT_LISTEN,
        VARLINK_ERROR_CANNOT_ACCEPT,
        VARLINK_ERROR_TYPE_MISMATCH,
        VARLINK_ERROR_INVALID_INDEX,
        VARLINK_ERROR_UNKNOWN_FIELD,
        VARLINK_ERROR_READ_ONLY,
        VARLINK_ERROR_INVALID_JSON,
        VARLINK_ERROR_SENDING_MESSAGE,
        VARLINK_ERROR_RECEIVING_MESSAGE,
        VARLINK_ERROR_INVALID_MESSAGE,
        VARLINK_ERROR_INVALID_CALL,
        VARLINK_ERROR_CONNECTION_CLOSED,
        VARLINK_ERROR_MAX
};

enum {
        VARLINK_CALL_MORE = 1
};

enum {
        VARLINK_REPLY_CONTINUES = 1
};

typedef struct VarlinkObject VarlinkObject;
typedef struct VarlinkArray VarlinkArray;

/*
 * Servers export one or more interfaces for clients to talks to.
 */
typedef struct VarlinkServer VarlinkServer;

typedef struct VarlinkCall VarlinkCall;

/*
 * Connections represent a communication channel between a VarlinkServer
 * and a VarlinkClient. A method call takes place over a connection, the
 * connection is busy until the method reply is received.
 */
typedef struct VarlinkConnection VarlinkConnection;

/*
 * The server installs a method callback for every method described in its
 * interfaces. The callback is executed when a client calls the respective
 * method.
 */
typedef long (*VarlinkMethodServerCallback)(VarlinkServer *server,
                                            VarlinkCall *call,
                                            VarlinkObject *parameters,
                                            uint64_t flags,
                                            void *userdata);

typedef void (*VarlinkConnectionClosedFunc)(VarlinkConnection *connection,
                                            void *userdata);

typedef void (*VarlinkCallCanceled)(VarlinkCall *call,
                                    void *userdata);

long varlink_object_new(VarlinkObject **objectp);
long varlink_object_new_from_json(VarlinkObject **objectp, const char *json);
VarlinkObject *varlink_object_ref(VarlinkObject *object);
VarlinkObject *varlink_object_unref(VarlinkObject *object);
void varlink_object_unrefp(VarlinkObject **objectp);

/*
 * Write the data of a VarlinkVariant formatted in JSON to a newly allocated
 * string. Escape sequences can be injected to e.g. colorize console output.
 *
 * Returns the length of the allocated string or a negative errno.
 */
long varlink_object_to_json(VarlinkObject *object, char **stringp);

unsigned long varlink_object_get_field_names(VarlinkObject *object, const char ***namesp);

long varlink_object_get_bool(VarlinkObject *object, const char *field, bool *bp);
long varlink_object_get_int(VarlinkObject *object, const char *field, int64_t *ip);
long varlink_object_get_float(VarlinkObject *object, const char *field, double *fp);
long varlink_object_get_string(VarlinkObject *object, const char *field, const char **stringp);
long varlink_object_get_array(VarlinkObject *object, const char *field, VarlinkArray **arrayp);
long varlink_object_get_object(VarlinkObject *object, const char *field, VarlinkObject **nestedp);

long varlink_object_set_bool(VarlinkObject *object, const char *field, bool b);
long varlink_object_set_int(VarlinkObject *object, const char *field, int64_t i);
long varlink_object_set_float(VarlinkObject *object, const char *field, double f);
long varlink_object_set_string(VarlinkObject *object, const char *field, const char *string);
long varlink_object_set_array(VarlinkObject *object, const char *field, VarlinkArray *array);
long varlink_object_set_object(VarlinkObject *object, const char *field, VarlinkObject *nested);

/*
 * Creates a new VarlinkArray. The type in typestring must be an array type.
 */
long varlink_array_new(VarlinkArray **arrayp);

/*
 * Increment the reference count of a VarlinkArray.
 *
 * Returns the same VarlinkArray.
 */
VarlinkArray *varlink_array_ref(VarlinkArray *array);

/*
 * Decrement the reference count of a VarlinkArray. Dropping the last
 * reference frees all ressources.
 *
 * Returns NULL;
 */
VarlinkArray *varlink_array_unref(VarlinkArray *array);

/*
 * varlink_array_unref() to be used with the cleanup attribute.
 */
void varlink_array_unrefp(VarlinkArray **arrayp);

/*
 * Returns the number of elements of an array.
 */
unsigned long varlink_array_get_n_elements(VarlinkArray *array);

/*
 * Extract the basic value of the array element at index.
 *
 * Return 0 or a negative errno.
 *
 * Possible errors:
 *   EINVAL: the variant is not an array or the given index is out of bounds
 *   EBADMSG: the variant's underlying data is corrupt
 */
long varlink_array_get_bool(VarlinkArray *array, unsigned long index, bool *bp);
long varlink_array_get_int(VarlinkArray *array, unsigned long index, int64_t *ip);
long varlink_array_get_float(VarlinkArray *array, unsigned long index, double *fp);
long varlink_array_get_string(VarlinkArray *array, unsigned long index, const char **stringp);
long varlink_array_get_array(VarlinkArray *array, unsigned long index, VarlinkArray **elementp);
long varlink_array_get_object(VarlinkArray *array, unsigned long index, VarlinkObject **objectp);

/*
 * Appends a value to the end of an array.
 *
 * Return 0 or a negative errno.
 *
 * Possible errors:
 *   EINVAL: the variant is not an array or the element has the wrong type
 */
long varlink_array_append_bool(VarlinkArray *array, bool b);
long varlink_array_append_int(VarlinkArray *array, int64_t i);
long varlink_array_append_float(VarlinkArray *array, double f);
long varlink_array_append_string(VarlinkArray *array, const char *string);
long varlink_array_append_array(VarlinkArray *array, VarlinkArray *element);
long varlink_array_append_object(VarlinkArray *array, VarlinkObject *object);

/*
 * Create a new server with the given name, the human readbale description,
 * a list of key/value properties, from an array of interface strings.
 *
 * Returns 0 or a negative errno.
 */
long varlink_server_new(VarlinkServer **serverp,
                        const char *address,
                        int listen_fd,
                        const char *name,
                        VarlinkObject *properties,
                        const char **interfacestrings, unsigned long n_interfaces);

/*
 * Destroys a VarlinkServer, close all its connections and free all its
 * ressources.
 *
 * Returns NULL
 */
VarlinkServer *varlink_server_free(VarlinkServer *server);

/*
 * varlink_server_free() to be used with the cleanup attribute.
 */
void varlink_server_freep(VarlinkServer **serverp);

/*
 * Install a callback for the specified method, to be called whenever a client
 * send a method call.
 *
 * Returns 0 or a negative errno.
 */
long varlink_server_set_method_callback(VarlinkServer *server,
                                        const char *qualified_method,
                                        VarlinkMethodServerCallback callback,
                                        void *callback_userdata);

/*
 * Get the file descriptor to integrate with poll() into a mainloop; it becomes
 * readable whenever there is pending data, like a method call from a client.
 *
 * Returns the file descriptor or a negative errno.
 */
int varlink_server_get_fd(VarlinkServer *server);

int varlink_server_get_listen_fd(VarlinkServer *server);

/*
 * Process pending events in the VarlinkServer. It needs to be called whenever
 * the file descriptor becomes readable. Method calls are dispatched according
 * to their installed callbacks.
 *
 * Returns 0 or a negative errno.
 */
long varlink_server_process_events(VarlinkServer *server);

VarlinkCall *varlink_call_ref(VarlinkCall *call);
VarlinkCall *varlink_call_unref(VarlinkCall *call);
void varlink_call_unrefp(VarlinkCall **callp);

/*
 * Sets a function which is called when the client cancels a call (i.e.,
 * closes the connection).
 *
 * Only one such function can be set.
 */
long varlink_call_set_canceled_callback(VarlinkCall *call,
                                        VarlinkCallCanceled callback,
                                        void *userdata);

/*
 * Reply to a method call. After this function, the call is finished.
 */
long varlink_call_reply(VarlinkCall *call,
                        VarlinkObject *parameters,
                        uint64_t flags);

long varlink_call_reply_error(VarlinkCall *call,
                              const char *error,
                              VarlinkObject *parameters);

__attribute__((sentinel))
long varlink_call_reply_invalid_parameters(VarlinkCall *call, ...);

long varlink_connection_new(VarlinkConnection **connectionp, const char *address);

void varlink_connection_set_close_callback(VarlinkConnection *connection,
                                           VarlinkConnectionClosedFunc closed,
                                           void *userdata);

int varlink_connection_get_fd(VarlinkConnection *connection);
long varlink_connection_process_events(VarlinkConnection *connection, int events);
int varlink_connection_get_events(VarlinkConnection *connection);

long varlink_connection_receive_reply(VarlinkConnection *connection,
                                      VarlinkObject **parametersp,
                                      char **errorp,
                                      long *flagsp);

/*
 * Call the specified method with the given argument. The reply will execute
 * the given callback.
 *
 * Returns 0 or a negative errno.
 */
long varlink_connection_call(VarlinkConnection *connection,
                             const char *qualified_method,
                             VarlinkObject *parameters,
                             uint64_t flags);

/*
 * Closes @connection and frees it.
 */
VarlinkConnection *varlink_connection_close(VarlinkConnection *connection);

void varlink_connection_closep(VarlinkConnection **connectionp);

#ifdef __cplusplus
}
#endif
