// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define _cleanup_(_x) __attribute__((__cleanup__(_x)))
#define _public_ __attribute__((__visibility__("default")))

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ x
#else
# define UNUSED(x) x
#endif

static inline void freep(void *p) {
        free(*(void **)p);
}

static inline void closep(const int *fd) {
        if (*fd >= 0)
                close(*fd);
}

static inline void fclosep(FILE **fp) {
        if (*fp)
                fclose(*fp);
}

static inline void close_fds(const int *fds, unsigned long n_fds) {
        for (unsigned long i = 0; i < n_fds; i += 1)
                if (fds[i] >= 0)
                        close(fds[i]);
}

/* Closes the descriptors, releases the array and resets both */
static inline void close_and_free_fds(int **fdsp, unsigned long *n_fdsp) {
        close_fds(*fdsp, *n_fdsp);

        free(*fdsp);
        *fdsp = NULL;
        *n_fdsp = 0;
}

int epoll_add(int epfd, int fd, uint32_t events, void *ptr);
int epoll_mod(int epfd, int fd, uint32_t events, void *ptr);
int epoll_del(int epfd, int fd);

#define MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))
#define MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define ARRAY_SIZE(_x) (sizeof(_x) / sizeof((_x)[0]))
#define ALIGN_TO(_val, _to) (((_val) + (_to) - 1) & ~((_to) - 1))
