#include "transport.h"
#include "util.h"
#include "varlink.h"

#include <string.h>

int varlink_transport_listen(VarlinkURI *uri, char **pathp) {
        switch (uri->type) {
                case VARLINK_URI_PROTOCOL_TCP:
                        return varlink_listen_tcp(uri->host);

                case VARLINK_URI_PROTOCOL_UNIX:
                        return varlink_listen_unix(uri->path, pathp);

                case VARLINK_URI_PROTOCOL_DEVICE:
                case VARLINK_URI_PROTOCOL_NONE:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        abort();
}

_public_ int varlink_listen(const char *address, char **pathp) {
        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
        int r;

        r = varlink_uri_new(&uri, address, false);
        if (r < 0)
                return r;

        return varlink_transport_listen(uri, pathp);
}

int varlink_transport_accept(VarlinkURI *uri, int listen_fd) {
        switch (uri->type) {
                case VARLINK_URI_PROTOCOL_TCP:
                        return varlink_accept_tcp(listen_fd);

                case VARLINK_URI_PROTOCOL_UNIX:
                        return varlink_accept_unix(listen_fd);

                case VARLINK_URI_PROTOCOL_DEVICE:
                case VARLINK_URI_PROTOCOL_NONE:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        abort();
}

int varlink_transport_connect(VarlinkURI *uri) {
        switch (uri->type) {
                case VARLINK_URI_PROTOCOL_DEVICE:
                        return varlink_connect_device(uri->path);

                case VARLINK_URI_PROTOCOL_TCP:
                        return varlink_connect_tcp(uri->host);

                case VARLINK_URI_PROTOCOL_UNIX:
                        return varlink_connect_unix(uri->path);

                case VARLINK_URI_PROTOCOL_NONE:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        abort();
}
