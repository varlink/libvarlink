#pragma once

#include "varlink.h"

long varlink_protocol_pack_call(const char *method,
                                VarlinkObject *parameters,
                                uint64_t flags,
                                VarlinkObject **callp);

long varlink_protocol_unpack_call(VarlinkObject *call,
                                  char **methodp,
                                  VarlinkObject **parametersp,
                                  uint64_t *flagsp);

long varlink_protocol_pack_reply(const char *error,
                                 VarlinkObject *parameters,
                                 uint64_t flags,
                                 VarlinkObject **replyp);

long varlink_protocol_unpack_reply(VarlinkObject *reply,
                                   char **errorp,
                                   VarlinkObject **parametersp,
                                   uint64_t *flagsp);
