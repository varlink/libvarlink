#include "error.h"
#include "util.h"
#include "varlink.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *error_strings[] = {
        [VARLINK_ERROR_PANIC] = "Panic",
        [VARLINK_ERROR_INVALID_INTERFACE] ="InvalidInterface",
        [VARLINK_ERROR_INVALID_ADDRESS] ="InvalidAddress",
        [VARLINK_ERROR_INVALID_METHOD] ="InvalidMethod",
        [VARLINK_ERROR_DUPLICATE_INTERFACE] ="DuplicateInterface",
        [VARLINK_ERROR_INTERFACE_NOT_FOUND] ="InterfaceNotFound",
        [VARLINK_ERROR_METHOD_NOT_FOUND] ="MethodNotFound",
        [VARLINK_ERROR_CANNOT_CONNECT] ="CannotConnect",
        [VARLINK_ERROR_CANNOT_LISTEN] ="CannotListen",
        [VARLINK_ERROR_CANNOT_ACCEPT] ="CannotAccept",
        [VARLINK_ERROR_TYPE_MISMATCH] ="TypeMismatch",
        [VARLINK_ERROR_INVALID_INDEX] ="InvalidIndex",
        [VARLINK_ERROR_UNKNOWN_FIELD] ="UnknownField",
        [VARLINK_ERROR_INVALID_VALUE] ="InvalidValue",
        [VARLINK_ERROR_READ_ONLY] ="ReadOnly",
        [VARLINK_ERROR_INVALID_JSON] ="InvalidJson",
        [VARLINK_ERROR_SENDING_MESSAGE] ="SendingMessage",
        [VARLINK_ERROR_RECEIVING_MESSAGE] ="ReceivingMessage",
        [VARLINK_ERROR_INVALID_MESSAGE] ="InvalidMessage",
        [VARLINK_ERROR_INVALID_CALL] ="InvalidCall",
        [VARLINK_ERROR_CONNECTION_CLOSED] ="ConnectionClosed",
        [VARLINK_ERROR_NOT_AVAILABLE] ="NotAvailable",
        [VARLINK_ERROR_INVALID_TYPE] = "InvalidType",
};

const char *varlink_error_string(long error) {
        if (error == 0 || error >= (long)ARRAY_SIZE(error_strings))
                return "<invalid>";

        if (!error_strings[error])
                return "<missing>";

        return error_strings[error];
}
