#include "uri.h"
#include "util.h"
#include "varlink.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

VarlinkURI *varlink_uri_free(VarlinkURI *uri) {
        free(uri->protocol);
        free(uri->host);
        free(uri->path);
        free(uri->qualified_member);
        free(uri->interface);
        free(uri->member);
        free(uri->query);
        free(uri->fragment);
        free(uri);

        return NULL;
}

void varlink_uri_freep(VarlinkURI **urip) {
        if (*urip)
                varlink_uri_free(*urip);
}

static long string_percent_decode( const char *in, char **outp) {
        unsigned long len;
        _cleanup_(freep) char *out = NULL;
        unsigned long j = 0;

        len = strlen(in);
        out = malloc(len + 1);

        for (unsigned long i = 0; in[i] != '\0' && i < len; i += 1) {
                if (in[i] == '%') {
                        unsigned int hex;

                        if (i + 3 > len)
                                return -VARLINK_ERROR_INVALID_ADDRESS;

                        if (sscanf(in + i + 1, "%02x", &hex) != 1)
                                return -VARLINK_ERROR_INVALID_ADDRESS;

                        out[j] = hex;
                        j += 1;
                        i += 2;

                        continue;
                }

                out[j] = in[i];
                j += 1;
        }

        out[j] = '\0';
        *outp = out;
        out = NULL;

        return j;
}

static void uri_parse_protocol(VarlinkURI *uri, const char *address, char **stringp) {
        if (strncmp(address, "device:", 7) == 0) {
                uri->type = VARLINK_URI_PROTOCOL_DEVICE;
                uri->protocol = strdup("device");
                *stringp = strdup(address + 7);
                return;
        }

        if (strncmp(address, "exec:", 5) == 0) {
                uri->type = VARLINK_URI_PROTOCOL_EXEC;
                uri->protocol = strdup("exec");
                *stringp = strdup(address + 5);
                return;
        }

        if (strncmp(address, "ssh://", 6) == 0) {
                uri->type = VARLINK_URI_PROTOCOL_SSH;
                uri->protocol = strdup("ssh");
                *stringp = strdup(address + 6);
                return;
        }

        if (strncmp(address, "unix:", 5) == 0) {
                uri->type = VARLINK_URI_PROTOCOL_UNIX;
                uri->protocol = strdup("unix");
                *stringp = strdup(address + 5);
                return;
        }

        /* If no protocol is specified assume IP/host address */
        if (strchr(address, '/'))
                uri->type = VARLINK_URI_PROTOCOL_IP;

        /* interface/member only */
        *stringp = strdup(address);
}

/*
 * For protocols which define an URI path, we cannot distinguish
 * between the last path segment and an interface. The caller needs
 * to supply this information in `has_interface`, depending on the
 * context the URI is parsed.
 *
 * ssh://example.org:22223/org.example.foo.List?foo=bar#baz
 *   address:          ssh://example.org:22223
 *   protocol:         ssh
 *   host:             example.org:22223
 *   qualified_member: org.example.foo.List
 *   interface:        org.example.foo
 *   member:           List
 *   query:            foo=bar
 *   fragment:         baz
 *
 * unix:/run/org.example.foo/org.example.foo#baz
 *   address:          unix:/run/org.example.foo
 *   protocol:         unix
 *   path:             /run/org.example.foo
 *   interface:        org.example.foo
 *   fragment:         baz
 *
 * exec:io.systemd.sysinfo/org.example.foo
 *   address:          exec:io.systemd.sysinfo
 *   protocol:         exec
 *   path:             io.systemd.sysinfo
 *   interface:        org.example.foo
 */
long varlink_uri_new(VarlinkURI **urip, const char *address, bool has_interface) {
        _cleanup_(varlink_uri_freep) VarlinkURI *uri = NULL;
        _cleanup_(freep) char *string = NULL;
        char *p;
        long r;

        uri = calloc(1, sizeof(VarlinkURI));

        uri_parse_protocol(uri, address, &string);

        /* Split URI fragment */
        p = strchr(string, '#');
        if (p) {
                char *s = string;

                uri->fragment = strdup(p + 1);

                s = strndup(string, p - string);
                free(string);
                string = s;
        }

        /* Split URI query */
        p = strchr(string, '?');
        if (p) {
                char *s = string;

                uri->query = strdup(p + 1);

                s = strndup(string, p - string);
                free(string);
                string = s;
        }

        if (has_interface) {
                char *s = string;

                p = strrchr(string, '/');
                if (p) {
                        /* Split varlink interface + member */
                        uri->interface = strdup(p + 1);

                        s = strndup(string, p - string);
                        free(string);
                        string = s;

                } else {
                        /* No path or host */
                        uri->interface = string;
                        string = NULL;
                }

                p = strrchr(uri->interface, '.');
                if (!p)
                        return -VARLINK_ERROR_INVALID_INTERFACE;

                if (p[1] >= 'A' && p[1] <= 'Z') {
                        /* Split interface and member */
                        uri->qualified_member = uri->interface;
                        uri->interface = strndup(uri->interface, p - uri->interface);
                        uri->member = strdup(p + 1);

                } else if (p[1] == '\0') {
                        /* Interface only, remove trailing dot */
                        *p = '\0';
                }
        }

        /* Depending on the protocol, we have an URI path or an URI host*/
        switch(uri->type) {
                case VARLINK_URI_PROTOCOL_DEVICE:
                case VARLINK_URI_PROTOCOL_EXEC:
                case VARLINK_URI_PROTOCOL_UNIX:
                        if (!string)
                                return -VARLINK_ERROR_INVALID_ADDRESS;

                        r = string_percent_decode(string, &uri->path);
                        if (r < 0)
                                return r;
                        break;

                case VARLINK_URI_PROTOCOL_IP:
                case VARLINK_URI_PROTOCOL_SSH:
                        if (!string || strchr(string, '/'))
                                return -VARLINK_ERROR_INVALID_ADDRESS;

                        r = string_percent_decode(string, &uri->host);
                        if (r < 0)
                                return r;
                        break;

                case VARLINK_URI_PROTOCOL_NONE:
                        break;
        }

        *urip = uri;
        uri = NULL;

        return 0;
}
