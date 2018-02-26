#include "transport.h"
#include "util.h"
#include "varlink.h"

#include <string.h>

int varlink_transport_listen(VarlinkURI *uri, char **pathp) {
        switch (uri->type) {
                case VARLINK_URI_PROTOCOL_TCP:
                        return varlink_listen_ip(uri->host);

                case VARLINK_URI_PROTOCOL_UNIX:
                        return varlink_listen_unix(uri->path, pathp);

                case VARLINK_URI_PROTOCOL_DEVICE:
                case VARLINK_URI_PROTOCOL_EXEC:
                case VARLINK_URI_PROTOCOL_NONE:
                case VARLINK_URI_PROTOCOL_SSH:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        abort();
}

_public_ int varlink_listen(const char *address, char **pathp) {
        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
        _cleanup_(freep) char *destination = NULL;
        long r;

        r = varlink_uri_new(&uri, address, false);
        if (r < 0)
                return r;

        return varlink_transport_listen(uri, pathp);
}

int varlink_transport_accept(VarlinkURI *uri, int listen_fd) {
        switch (uri->type) {
                case VARLINK_URI_PROTOCOL_TCP:
                        return varlink_accept_ip(listen_fd);

                case VARLINK_URI_PROTOCOL_UNIX:
                        return varlink_accept_unix(listen_fd);

                case VARLINK_URI_PROTOCOL_DEVICE:
                case VARLINK_URI_PROTOCOL_EXEC:
                case VARLINK_URI_PROTOCOL_NONE:
                case VARLINK_URI_PROTOCOL_SSH:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        abort();
}

int varlink_transport_connect(VarlinkURI *uri, pid_t *pidp) {
        switch (uri->type) {
                case VARLINK_URI_PROTOCOL_DEVICE:
                        return varlink_connect_device(uri->path);

                case VARLINK_URI_PROTOCOL_EXEC:
                        return varlink_connect_exec(uri->path, pidp);

                case VARLINK_URI_PROTOCOL_SSH:
                        return varlink_connect_ssh(uri->host, pidp);

                case VARLINK_URI_PROTOCOL_TCP:
                        return varlink_connect_ip(uri->host);

                case VARLINK_URI_PROTOCOL_UNIX:
                        return varlink_connect_unix(uri->path);

                case VARLINK_URI_PROTOCOL_NONE:
                        return -VARLINK_ERROR_INVALID_ADDRESS;
        }

        abort();
}
