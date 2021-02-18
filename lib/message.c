// SPDX-License-Identifier: Apache-2.0

#include "message.h"
#include "util.h"

#include <string.h>

long varlink_message_pack_call(const char *method,
                               VarlinkObject *parameters,
                               uint64_t flags,
                               VarlinkObject **callp) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *call = NULL;
        long r;

        if (flags & VARLINK_CALL_MORE && flags & VARLINK_CALL_ONEWAY)
                return -VARLINK_ERROR_INVALID_CALL;

        r = varlink_object_new(&call);
        if (r < 0)
                return r;

        r = varlink_object_set_string(call, "method", method);
        if (r < 0)
                return r;

        if (parameters) {
                r = varlink_object_set_object(call, "parameters", parameters);
                if (r < 0)
                        return r;
        }

        if (flags & VARLINK_CALL_MORE) {
                r = varlink_object_set_bool(call, "more", true);
                if (r < 0)
                        return r;
        }

        if (flags & VARLINK_CALL_ONEWAY) {
                r = varlink_object_set_bool(call, "oneway", true);
                if (r < 0)
                        return r;
        }

        *callp = call;
        call = NULL;

        return 0;
}

long varlink_message_unpack_call(VarlinkObject *call,
                                 char **methodp,
                                 VarlinkObject **parametersp,
                                 uint64_t *flagsp) {
        const char *method;
        VarlinkObject *parameters = NULL;
        _cleanup_(freep) char *m = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *p = NULL;
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

        m = strdup(method);
        if (!m)
                return -VARLINK_ERROR_PANIC;

        if (parameters)
                p = varlink_object_ref(parameters);
        else {
                r = varlink_object_new(&p);
                if (r < 0)
                        return r;
        }

        *methodp = m;
        m = NULL;

        *parametersp = p;
        p = NULL;

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
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        long r;

        r = varlink_object_new(&reply);
        if (r < 0)
                return r;

        if (error) {
                r = varlink_object_set_string(reply, "error", error);
                if (r < 0)
                        return r;
        }

        if (parameters) {
                r = varlink_object_set_object(reply, "parameters", parameters);
                if (r < 0)
                        return r;
        }

        if (flags & VARLINK_REPLY_CONTINUES) {
                r = varlink_object_set_bool(reply, "continues", true);
                if (r < 0)
                        return r;
        }

        *replyp = reply;
        reply = NULL;

        return 0;
}

long varlink_message_unpack_reply(VarlinkObject *reply,
                                  char **errorp,
                                  VarlinkObject **parametersp,
                                  uint64_t *flagsp) {
        const char *error = NULL;
        VarlinkObject *parameters = NULL;
        _cleanup_(freep) char *e = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *p = NULL;
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

        if (error) {
                e = strdup(error);
                if (!e)
                        return -VARLINK_ERROR_PANIC;
        }

        if (parameters)
                p = varlink_object_ref(parameters);
        else {
                r = varlink_object_new(&p);
                if (r < 0)
                        return r;
        }

        *errorp = e;
        e = NULL;

        *parametersp = p;
        p = NULL;

        *flagsp = 0;
        if (continues)
                *flagsp |= VARLINK_REPLY_CONTINUES;

        return 0;
}
