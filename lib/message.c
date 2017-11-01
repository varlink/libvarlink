#include "message.h"

#include <string.h>

long varlink_message_pack_call(const char *method,
                               VarlinkObject *parameters,
                               uint64_t flags,
                               VarlinkObject **callp) {
        VarlinkObject *call;

        if (flags & VARLINK_CALL_MORE && flags & VARLINK_CALL_ONEWAY)
                return -VARLINK_ERROR_INVALID_CALL;

        varlink_object_new(&call);
        varlink_object_set_string(call, "method", method);

        if (parameters)
                varlink_object_set_object(call, "parameters", parameters);

        if (flags & VARLINK_CALL_MORE)
                varlink_object_set_bool(call, "more", true);

        if (flags & VARLINK_CALL_ONEWAY)
                varlink_object_set_bool(call, "oneway", true);

        *callp = call;

        return 0;
}

long varlink_message_unpack_call(VarlinkObject *call,
                                 char **methodp,
                                 VarlinkObject **parametersp,
                                 uint64_t *flagsp) {
        const char *method;
        VarlinkObject *parameters = NULL;
        bool more = false;
        bool oneway = false;
        long r;

        r = varlink_object_get_string(call, "method", &method);
        if (r < 0)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        r = varlink_object_get_object(call, "parameters", &parameters);
        if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        r = varlink_object_get_bool(call, "more", &more);
        if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        r = varlink_object_get_bool(call, "oneway", &oneway);
        if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        *methodp = strdup(method);

        if (parameters)
                *parametersp = varlink_object_ref(parameters);
        else
                varlink_object_new(parametersp);

        *flagsp = 0;
        if (more)
                *flagsp |= VARLINK_CALL_MORE;
        if (oneway)
                *flagsp |= VARLINK_CALL_ONEWAY;

        return 0;
}

long varlink_message_pack_reply(const char *error,
                                VarlinkObject *parameters,
                                uint64_t flags,
                                VarlinkObject **replyp) {
        VarlinkObject *reply;

        varlink_object_new(&reply);

        if (error)
                varlink_object_set_string(reply, "error", error);

        if (parameters)
                varlink_object_set_object(reply, "parameters", parameters);

        if (flags & VARLINK_REPLY_CONTINUES)
                varlink_object_set_bool(reply, "continues", true);

        *replyp = reply;

        return 0;
}

long varlink_message_unpack_reply(VarlinkObject *reply,
                                  char **errorp,
                                  VarlinkObject **parametersp,
                                  uint64_t *flagsp) {
        const char *error = NULL;
        VarlinkObject *parameters = NULL;
        bool continues = false;
        long r;

        r = varlink_object_get_string(reply, "error", &error);
        if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        r = varlink_object_get_object(reply, "parameters", &parameters);
        if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        r = varlink_object_get_bool(reply, "continues", &continues);
        if (r < 0 && r != -VARLINK_ERROR_UNKNOWN_FIELD)
                return -VARLINK_ERROR_INVALID_MESSAGE;

        *errorp = error ? strdup(error) : NULL;

        if (parameters)
                *parametersp = varlink_object_ref(parameters);
        else
                varlink_object_new(parametersp);

        *flagsp = 0;
        if (continues)
                *flagsp |= VARLINK_REPLY_CONTINUES;

        return 0;
}
