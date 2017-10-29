#pragma once

long varlink_uri_percent_decode(char **outp, const char *in, unsigned long len);
long varlink_uri_split(const char *uri,
                       char **addressp,
                       char **qualified_memberp,
                       char **interfacep,
                       char **memberp);
