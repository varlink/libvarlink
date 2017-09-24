#pragma once

#include <varlink.h>

long varlink_connection_new_from_socket(VarlinkConnection **connectionp, int socket);
