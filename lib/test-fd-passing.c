// SPDX-License-Identifier: Apache-2.0

#include "stream.h"
#include "varlink.h"
#include "util.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>

#define SENT_BYTE 0x42
#define REPLIED_BYTE 0x43
#define WRITTEN_BACK_BYTE 0x5a

typedef struct {
        VarlinkService *service;
        VarlinkConnection *connection;
        int epoll_fd;
} Test;

static struct stat sent_st;
static struct stat replied_st;

/* A pipe whose read end holds one byte, with the identity to compare against later */
static int pipe_with_byte(uint8_t byte, struct stat *st) {
        int fds[2];

        assert(pipe(fds) == 0);
        assert(write(fds[1], &byte, 1) == 1);
        close(fds[1]);
        assert(fstat(fds[0], st) == 0);

        return fds[0];
}

/* Same pipe, close-on-exec, still holding its byte and nothing more */
static void assert_fd_works(int fd, const struct stat *original, uint8_t expect) {
        struct stat st;
        uint8_t byte;

        assert(fd >= 0);
        assert(fstat(fd, &st) == 0);
        assert(st.st_dev == original->st_dev);
        assert(st.st_ino == original->st_ino);
        assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);

        assert(read(fd, &byte, 1) == 1);
        assert(byte == expect);
        assert(read(fd, &byte, 1) == 0);
}

static unsigned long count_open_fds(void) {
        unsigned long n = 0;
        DIR *dir;

        dir = opendir("/proc/self/fd");
        assert(dir != NULL);

        while (readdir(dir))
                n += 1;

        closedir(dir);
        return n;
}

static long fill_socket(int fd) {
        char junk[4096] = {};
        long total = 0, n;

        while ((n = write(fd, junk, sizeof(junk))) > 0)
                total += n;
        assert(errno == EAGAIN);

        return total;
}

static void drain_socket(int fd, long total) {
        char junk[4096];
        long done = 0, n;

        while (done < total) {
                n = read(fd, junk, sizeof(junk));
                if (n > 0)
                        done += n;
                else
                        assert(errno == EAGAIN);
        }
}

static void write_named_with_fd(VarlinkStream *stream, const char *name, uint8_t byte, struct stat *st) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *o = NULL;
        int *fds;

        fds = malloc(sizeof(int));
        assert(fds != NULL);
        fds[0] = pipe_with_byte(byte, st);

        assert(varlink_object_new(&o) == 0);
        assert(varlink_object_set_string(o, "name", name) == 0);
        assert(varlink_stream_write_with_fds(stream, o, fds, 1) >= 0);
}

/*
 * Both messages are queued while the socket is full, so they leave together and
 * each has to keep its own descriptors.
 */
static void test_batch_per_message(void) {
        static const char *names[] = { "A", "B" };
        static const uint8_t bytes[] = { SENT_BYTE, REPLIED_BYTE };
        struct stat st[ARRAY_SIZE(names)];
        VarlinkStream *w, *r;
        int sv[2], sndbuf = 2048;
        long filled;

        assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0);
        assert(setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);

        assert(varlink_stream_new(&w, sv[0], false) == 0);
        assert(varlink_stream_new(&r, sv[1], false) == 0);
        assert(varlink_stream_set_allow_fd_passing_output(w, true) == 0);
        assert(varlink_stream_set_allow_fd_passing_input(r, true) == 0);

        filled = fill_socket(sv[0]);

        for (unsigned long i = 0; i < ARRAY_SIZE(names); i += 1)
                write_named_with_fd(w, names[i], bytes[i], &st[i]);

        drain_socket(sv[1], filled);

        for (int i = 0; i < 100; i += 1)
                assert(varlink_stream_flush(w) >= 0);

        for (unsigned long i = 0; i < ARRAY_SIZE(names); i += 1) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *o = NULL;
                const char *name;
                int fd;

                for (int j = 0; j < 100 && !o; j += 1)
                        assert(varlink_stream_read(r, &o) >= 0);
                assert(o != NULL);

                assert(varlink_object_get_string(o, "name", &name) == 0);
                assert(strcmp(name, names[i]) == 0);
                assert(varlink_stream_get_n_in_fds(r) == 1);

                fd = varlink_stream_take_in_fd(r, 0);
                assert_fd_works(fd, &st[i], bytes[i]);
                close(fd);
        }

        assert(varlink_stream_free(w) == NULL);
        assert(varlink_stream_free(r) == NULL);
}

static long org_varlink_example_PassFd(VarlinkService *UNUSED(service),
                                       VarlinkCall *call,
                                       VarlinkObject *parameters,
                                       uint64_t UNUSED(flags),
                                       void *UNUSED(userdata)) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        _cleanup_(closep) int received = -1;
        _cleanup_(closep) int sink = -1;
        uint8_t byte = WRITTEN_BACK_BYTE;
        int64_t index, sink_index;

        assert(varlink_object_get_int(parameters, "fd", &index) == 0);
        assert(varlink_object_get_int(parameters, "sink", &sink_index) == 0);
        assert(varlink_call_get_n_fds(call) == 2);

        assert(varlink_call_peek_fd(call, 2) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_call_peek_fd(call, (unsigned long) index) >= 0);

        received = varlink_call_take_fd(call, (unsigned long) index);
        assert(varlink_call_take_fd(call, (unsigned long) index) == -VARLINK_ERROR_INVALID_INDEX);
        assert_fd_works(received, &sent_st, SENT_BYTE);

        /* has to surface on the read end the caller kept */
        sink = varlink_call_take_fd(call, (unsigned long) sink_index);
        assert(sink >= 0);
        assert(write(sink, &byte, 1) == 1);

        assert(varlink_call_push_fd(call, pipe_with_byte(REPLIED_BYTE, &replied_st)) == 0);

        assert(varlink_object_new(&out) == 0);
        assert(varlink_object_set_int(out, "fd", 0) == 0);

        return varlink_call_reply(call, out, 0);
}

static long org_varlink_example_NoFd(VarlinkService *UNUSED(service),
                                     VarlinkCall *call,
                                     VarlinkObject *UNUSED(parameters),
                                     uint64_t UNUSED(flags),
                                     void *UNUSED(userdata)) {
        assert(varlink_call_get_n_fds(call) == 0);
        assert(varlink_call_peek_fd(call, 0) == -VARLINK_ERROR_INVALID_INDEX);

        return varlink_call_reply(call, NULL, 0);
}

static long test_process_events(Test *test) {
        struct epoll_event events[2];
        long n;

        assert(epoll_mod(test->epoll_fd,
                         varlink_connection_get_fd(test->connection),
                         varlink_connection_get_events(test->connection),
                         test->connection) == 0);

        n = epoll_wait(test->epoll_fd, events, ARRAY_SIZE(events), 1000);
        assert(n > 0);

        for (long i = 0; i < n; i += 1) {
                if (events[i].data.ptr == test->service)
                        assert(varlink_service_process_events(test->service) == 0);
                else if (events[i].data.ptr == test->connection)
                        assert(varlink_connection_process_events(test->connection, events[i].events) == 0);
                else
                        assert(false);
        }

        return 0;
}

static long done_callback(VarlinkConnection *UNUSED(connection),
                          const char *error,
                          VarlinkObject *UNUSED(parameters),
                          uint64_t UNUSED(flags),
                          void *userdata) {
        bool *done = userdata;

        assert(error == NULL);
        *done = true;

        return 0;
}

static long pass_fd_callback(VarlinkConnection *connection,
                             const char *error,
                             VarlinkObject *parameters,
                             uint64_t UNUSED(flags),
                             void *userdata) {
        bool *done = userdata;
        _cleanup_(closep) int received = -1;
        int64_t index;

        assert(error == NULL);
        assert(varlink_object_get_int(parameters, "fd", &index) == 0);
        assert(varlink_connection_get_n_fds(connection) == 1);

        received = varlink_connection_take_fd(connection, (unsigned long) index);
        assert_fd_works(received, &replied_st, REPLIED_BYTE);

        *done = true;
        return 0;
}

int main(void) {
        const char *interface = "interface org.varlink.example\n"
                                        "method PassFd(fd: int, sink: int) -> (fd: int)\n"
                                        "method NoFd() -> ()";

        Test test = {};
        unsigned long fds_at_start;

        fds_at_start = count_open_fds();

        test_batch_per_message();

        assert(varlink_service_new(&test.service,
                                   "Varlink", "Test Service", "1", "http://example.com",
                                   "unix:@test-fd-passing.socket",
                                   -1) == 0);
        assert(varlink_service_add_interface(test.service, interface,
                                             "PassFd", org_varlink_example_PassFd, NULL,
                                             "NoFd", org_varlink_example_NoFd, NULL,
                                             NULL) == 0);
        assert(varlink_service_set_allow_fd_passing_input(test.service, true) == 0);
        assert(varlink_service_set_allow_fd_passing_output(test.service, true) == 0);

        assert(varlink_connection_new(&test.connection, "unix:@test-fd-passing.socket") == 0);

        assert(varlink_connection_push_fd(test.connection, 0) == -VARLINK_ERROR_INVALID_CALL);

        assert(varlink_connection_set_allow_fd_passing_input(test.connection, true) == 0);
        assert(varlink_connection_set_allow_fd_passing_output(test.connection, true) == 0);

        test.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        assert(test.epoll_fd > 0);
        assert(epoll_add(test.epoll_fd,
                         varlink_service_get_fd(test.service),
                         EPOLLIN,
                         test.service) == 0);
        assert(epoll_add(test.epoll_fd,
                         varlink_connection_get_fd(test.connection),
                         varlink_connection_get_events(test.connection),
                         test.connection) == 0);

        /* round trip: two descriptors out, one back, one written through */
        {
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;
                _cleanup_(closep) int sink_read = -1;
                bool done = false;
                uint8_t byte;
                int sink[2];

                assert(pipe(sink) == 0);
                sink_read = sink[0];

                assert(varlink_connection_push_fd(test.connection, pipe_with_byte(SENT_BYTE, &sent_st)) == 0);
                assert(varlink_connection_push_fd(test.connection, sink[1]) == 0);

                assert(varlink_object_new(&parameters) == 0);
                assert(varlink_object_set_int(parameters, "fd", 0) == 0);
                assert(varlink_object_set_int(parameters, "sink", 1) == 0);
                assert(varlink_connection_call(test.connection, "org.varlink.example.PassFd", parameters, 0,
                                               pass_fd_callback, &done) == 0);

                for (long i = 0; !done && i < 10; i += 1)
                        assert(test_process_events(&test) == 0);
                assert(done);

                assert(read(sink_read, &byte, 1) == 1);
                assert(byte == WRITTEN_BACK_BYTE);
        }

        /* the reply callback has returned, so nothing is left to claim */
        assert(varlink_connection_get_n_fds(test.connection) == 0);
        assert(varlink_connection_peek_fd(test.connection, 0) == -VARLINK_ERROR_INVALID_INDEX);
        assert(varlink_connection_take_fd(test.connection, 0) == -VARLINK_ERROR_INVALID_INDEX);

        /* a peer that refuses descriptors rejects the send, or the kernel drops them */
        {
                bool done = false;
                long r;

                assert(varlink_service_set_allow_fd_passing_input(test.service, false) == 0);
                assert(varlink_connection_push_fd(test.connection, pipe_with_byte(SENT_BYTE, &sent_st)) == 0);

                r = varlink_connection_call(test.connection, "org.varlink.example.NoFd", NULL, 0,
                                            done_callback, &done);
                assert(r == 0 || r == -VARLINK_ERROR_SENDING_MESSAGE);

                for (long i = 0; r == 0 && !done && i < 10; i += 1)
                        assert(test_process_events(&test) == 0);
                assert(r != 0 || done);
        }

        assert(varlink_connection_free(test.connection) == NULL);
        assert(varlink_service_free(test.service) == NULL);
        close(test.epoll_fd);

        assert(count_open_fds() == fds_at_start);

        return EXIT_SUCCESS;
}
