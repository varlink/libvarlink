// SPDX-License-Identifier: Apache-2.0

#include "util.h"

#include <sys/epoll.h>

static int epoll_mod_internal(int epfd, int op, int fd, uint32_t events, void *ptr) {
        struct epoll_event event = {
                .events = events,
                .data = {
                        .ptr = ptr
                }
        };

        return epoll_ctl(epfd, op, fd, &event);
}

int epoll_add(int epfd, int fd, uint32_t events, void *ptr) {
        return epoll_mod_internal(epfd, EPOLL_CTL_ADD, fd, events, ptr);
}

int epoll_mod(int epfd, int fd, uint32_t events, void *ptr) {
        return epoll_mod_internal(epfd, EPOLL_CTL_MOD, fd, events, ptr);
}

int epoll_del(int epfd, int fd) {
        return epoll_mod_internal(epfd, EPOLL_CTL_DEL, fd, 0, NULL);
}
