#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Error codes retured by the library functions as negative values.
 */
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

/*
 * Keywords/flags of a method call.
 */
enum {
        VARLINK_CALL_MORE = 1
};

/*
 * Keywords/flags of a method reply.
 */
enum {
        VARLINK_REPLY_CONTINUES = 1
};

/*
 * Objects and arrays represent basic data types corresponding with JSON
 * objects and arrays.
 */
typedef struct VarlinkObject VarlinkObject;
typedef struct VarlinkArray VarlinkArray;

/*
 * A varlink service exports a set of interfaces and listens on a varlink
 * address for incoming calls.
 */
typedef struct VarlinkService VarlinkService;

/*
 * An incoming method call.
 */
typedef struct VarlinkCall VarlinkCall;

/*
 * A connection from a client to a service.
 */
typedef struct VarlinkConnection VarlinkConnection;

/*
 * Called when a client calls a method of a service.
 */
typedef long (*VarlinkMethodCallback)(VarlinkService *service,
                                      VarlinkCall *call,
                                      VarlinkObject *parameters,
                                      uint64_t flags,
                                      void *userdata);

/*
 * Called when a connection is closed.
 */
typedef void (*VarlinkConnectionClosedFunc)(VarlinkConnection *connection,
                                            void *userdata);

/*
 * Called when a client cancels a call.
 */
typedef void (*VarlinkCallCanceled)(VarlinkCall *call,
                                    void *userdata);

/*
 * Called when a client receives a reply to its call.
 */
typedef void (*VarlinkReplyFunc)(VarlinkConnection *connection,
                                 const char *error,
                                 VarlinkObject *parameters,
                                 uint64_t flags,
                                 void *userdata);
/*
 * Create a new empty object.
 */
long varlink_object_new(VarlinkObject **objectp);

/*
 * Createa new object by reading its data from a JSON string.
 */
long varlink_object_new_from_json(VarlinkObject **objectp, const char *json);

/*
 * Decrement the reference count of an array. Dropping the last
 * reference frees all ressources.
 *
 * Returns NULL;
 */
VarlinkObject *varlink_object_unref(VarlinkObject *object);

/*
 * varlink_object_unref() to be used with the cleanup attribute.
 */
void varlink_object_unrefp(VarlinkObject **objectp);

/*
 * Increment the reference count of an array.
 *
 * Returns the same VarlinkArray.
 */
VarlinkObject *varlink_object_ref(VarlinkObject *object);

/*
 * Write the data of an object formatted as JSON to a newly allocated
 * string.
 *
 * Returns the length of the allocated string or a negative VARLINK_ERROR.
 */
long varlink_object_to_json(VarlinkObject *object, char **stringp);

/*
 * Retrieve an array of strings with the filed names of the object.
 */
unsigned long varlink_object_get_field_names(VarlinkObject *object, const char ***namesp);

/*
 * Get values from an object.
 */
long varlink_object_get_bool(VarlinkObject *object, const char *field, bool *bp);
long varlink_object_get_int(VarlinkObject *object, const char *field, int64_t *ip);
long varlink_object_get_float(VarlinkObject *object, const char *field, double *fp);
long varlink_object_get_string(VarlinkObject *object, const char *field, const char **stringp);
long varlink_object_get_array(VarlinkObject *object, const char *field, VarlinkArray **arrayp);
long varlink_object_get_object(VarlinkObject *object, const char *field, VarlinkObject **nestedp);

/*
 * Set values of an object.
 */
long varlink_object_set_bool(VarlinkObject *object, const char *field, bool b);
long varlink_object_set_int(VarlinkObject *object, const char *field, int64_t i);
long varlink_object_set_float(VarlinkObject *object, const char *field, double f);
long varlink_object_set_string(VarlinkObject *object, const char *field, const char *string);
long varlink_object_set_array(VarlinkObject *object, const char *field, VarlinkArray *array);
long varlink_object_set_object(VarlinkObject *object, const char *field, VarlinkObject *nested);

/*
 * Create a new array.
 */
long varlink_array_new(VarlinkArray **arrayp);

/*
 * Increment the reference count of an array.
 *
 * Returns the same VarlinkArray.
 */
VarlinkArray *varlink_array_ref(VarlinkArray *array);

/*
 * Decrement the reference count of an array. Dropping the last
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
 * Extract a value of the array element at index.
 *
 * Returns 0 or a negative VARLINK_ERROR.
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
 * Return 0 or a negative VARLINK_ERROR.
 */
long varlink_array_append_bool(VarlinkArray *array, bool b);
long varlink_array_append_int(VarlinkArray *array, int64_t i);
long varlink_array_append_float(VarlinkArray *array, double f);
long varlink_array_append_string(VarlinkArray *array, const char *string);
long varlink_array_append_array(VarlinkArray *array, VarlinkArray *element);
long varlink_array_append_object(VarlinkArray *array, VarlinkObject *object);

/*
 * Create a new varlink service with the given name and version and
 * listen for requests on the given address.
 *
 * If listen_fd is not -1, it must be an fd that was created for the
 * same address with varlink_listen().
 *
 * Returns 0 or a a negative varlink error.
 */
long varlink_service_new(VarlinkService **servicep,
                         const char *vendor,
                         const char *product,
                         const char *version,
                         const char *url,
                         const char *address,
                         int listen_fd);

/*
 * Create a new varlink service that handles all incoming requests with
 * the supplied callback, i.e., varlink_service_add_interface() does not
 * work on this object.
 */
long varlink_service_new_raw(VarlinkService **servicep,
                             const char *address,
                             int listen_fd,
                             VarlinkMethodCallback callback,
                             void *userdata);

/*
 * Destroys a VarlinkService, close all its connections and free all its
 * ressources.
 *
 * Returns NULL
 */
VarlinkService *varlink_service_free(VarlinkService *service);

/*
 * varlink_service_free() to be used with the cleanup attribute.
 */
void varlink_service_freep(VarlinkService **servicep);

/*
 * Add an interface to the service and register callbacks for its
 * methods.
 *
 * Callbacks have to be given as three arguments each: the method name
 * (without the interface prefix), a VarlinkMethodCallback, and a
 * userdata.
 */
__attribute__((sentinel))
long varlink_service_add_interface(VarlinkService *service,
                                   const char *interface_description,
                                   ...);

/*
 * Get the file descriptor to integrate with poll() into a mainloop; it becomes
 * readable whenever there is pending data, like a method call from a client.
 *
 * Returns the file descriptor or a negative VARLINK_ERROR.
 */
int varlink_service_get_fd(VarlinkService *service);

/*
 * Create a listen file descriptor for a varlink address and return it.
 * If the address is for a unix domain socket in the file system, it's
 * path will be returned in pathp and should be unlinked after closing
 * the socket.
 */
int varlink_listen(const char *address, char **pathp);

/*
 * Process pending events in the VarlinkService. It needs to be called whenever
 * the file descriptor becomes readable. Method calls are dispatched according
 * to their installed callbacks.
 *
 * Returns 0 or a negative VARLINK_ERROR.
 */
long varlink_service_process_events(VarlinkService *service);

VarlinkCall *varlink_call_ref(VarlinkCall *call);
VarlinkCall *varlink_call_unref(VarlinkCall *call);
void varlink_call_unrefp(VarlinkCall **callp);

const char *varlink_call_get_method(VarlinkCall *call);

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

long varlink_call_reply_invalid_parameter(VarlinkCall *call, const char *parameter);

long varlink_connection_new(VarlinkConnection **connectionp, const char *address);

VarlinkConnection *varlink_connection_free(VarlinkConnection *connection);
void varlink_connection_freep(VarlinkConnection **connectionp);

void varlink_connection_set_close_callback(VarlinkConnection *connection,
                                           VarlinkConnectionClosedFunc closed,
                                           void *userdata);

int varlink_connection_get_fd(VarlinkConnection *connection);
long varlink_connection_process_events(VarlinkConnection *connection, int events);
int varlink_connection_get_events(VarlinkConnection *connection);

/*
 * Call the specified method with the given argument. The reply will execute
 * the given callback.
 *
 * Returns 0 or a negative VARLINK_ERROR.
 */
long varlink_connection_call(VarlinkConnection *connection,
                             const char *qualified_method,
                             VarlinkObject *parameters,
                             uint64_t flags,
                             VarlinkReplyFunc callback,
                             void *userdata);

/*
 * Closes @connection.
 */
long varlink_connection_close(VarlinkConnection *connection);

bool varlink_connection_is_closed(VarlinkConnection *connection);

#ifdef __cplusplus
}
#endif
