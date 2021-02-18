// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "varlink.h"

long varlink_message_pack_call(const char *method,
                               VarlinkObject *parameters,
                               uint64_t flags,
                               VarlinkObject **callp);

long varlink_message_unpack_call(VarlinkObject *call,
                                 char **methodp,
                                 VarlinkObject **parametersp,
                                 uint64_t *flagsp);

long varlink_message_pack_reply(const char *error,
                                VarlinkObject *parameters,
                                uint64_t flags,
                                VarlinkObject **replyp);

long varlink_message_unpack_reply(VarlinkObject *reply,
                                  char **errorp,
                                  VarlinkObject **parametersp,
                                  uint64_t *flagsp);
