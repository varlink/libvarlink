// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

typedef struct {
        enum {
                VARLINK_URI_PROTOCOL_NONE,
                VARLINK_URI_PROTOCOL_DEVICE,
                VARLINK_URI_PROTOCOL_TCP,
                VARLINK_URI_PROTOCOL_UNIX
        } type;
        char *protocol;
        char *host;
        char *path;
        char *qualified_member;
        char *interface;
        char *member;
        char *query;
        char *fragment;
} VarlinkURI;

long varlink_uri_new(VarlinkURI **urip, const char *uri, bool has_interface, bool has_member);
VarlinkURI *varlink_uri_free(VarlinkURI *uri);
void varlink_uri_freep(VarlinkURI **urip);
