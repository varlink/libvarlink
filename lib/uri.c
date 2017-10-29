#include "uri.h"
#include "util.h"
#include "varlink.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/*
 * ssh://example.org:22223/org.example.foo.List
 *   ssh://example.org:22223
 *   org.example.foo.List
 *   org.example.foo
 *   List
 *
 * unix:/run/org.example.foo/org.example.foo.List
 *   unix:/run/org.example.foo
 *   org.example.foo.List
 *   org.example.foo
 *   List
 *
 * exec:io.systemd.sysinfo/org.example.foo.List
 *   exec:io.systemd.sysinfo
 *   org.example.foo.List
 *   org.example.foo
 *   List
 *
 */
long varlink_uri_split(const char *uri,
                       char **addressp,
                       char **qualified_memberp,
                       char **interfacep,
                       char **memberp) {
        _cleanup_(freep) char *address = NULL;
        _cleanup_(freep) char *qualified_member = NULL;
        _cleanup_(freep) char *interface = NULL;
        _cleanup_(freep) char *member = NULL;
        const char *p;
        const char *dot;

        /* Separate address */
        p = strrchr(uri, '/');
        if (p) {
                address = strndup(uri, p - uri);
                qualified_member = strdup(p + 1);

        } else
                qualified_member = strdup(uri);

        dot = strrchr(qualified_member, '.');
        if (!dot)
                return -VARLINK_ERROR_INVALID_INTERFACE;

        /* Separate member from interface */
        if (dot[1] >= 'A' && dot[1] <= 'Z') {
                interface = strndup(uri, dot - qualified_member);
                member = strdup(dot + 1);

        } else if (dot[1] == '\0') {
                /* Interface only, remove trailing dot */
                interface = strndup(uri, dot - qualified_member);

        } else
                interface = strdup(qualified_member);

        if (addressp) {
                *addressp = address;
                address = NULL;
        }

        if (interfacep) {
                *interfacep = interface;
                interface = NULL;
        }

        if (qualified_memberp) {
                *qualified_memberp = qualified_member;
                qualified_member = NULL;
        }

        if (memberp) {
                *memberp = member;
                member = NULL;
        }

        return 0;
}

long varlink_uri_percent_decode(char **outp, const char *in, unsigned long len) {
        _cleanup_(freep) char *out = NULL;
        unsigned long j = 0;

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
