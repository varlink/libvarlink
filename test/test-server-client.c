#include "varlink.h"
#include "util.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

enum {
        TEST_ERROR_TIMEOUT
};

typedef struct {
        VarlinkServer *server;
        VarlinkConnection *connection;
        int epoll_fd;
} Test;

static long org_varlink_example_Echo(VarlinkServer *server,
                                     VarlinkCall *call,
                                     VarlinkObject *parameters,
                                     uint64_t flags,
                                     void *userdata) {
        VarlinkObject *out;
        const char *word;

        assert(varlink_object_get_string(parameters, "word", &word) == 0);

        assert(varlink_object_new(&out) == 0);
        assert(varlink_object_set_string(out, "word", word) == 0);

        assert(varlink_call_reply(call, out, 0) == 0);

        assert(varlink_object_unref(out) == NULL);
        return 0;
}

static long org_varlink_example_Later(VarlinkServer *server,
                                      VarlinkCall *call,
                                      VarlinkObject *parameters,
                                      uint64_t flags,
                                      void *userdata) {
        VarlinkCall **callp = userdata;

        assert(*callp == NULL);
        *callp = varlink_call_ref(call);

        return 0;
}

static long test_process_events(Test *test) {
        struct epoll_event events[2];
        long n;

        assert(epoll_mod(test->epoll_fd,
                         varlink_connection_get_fd(test->connection),
                         varlink_connection_get_events(test->connection),
                         test->connection) == 0);

        n = epoll_wait(test->epoll_fd, events, ARRAY_SIZE(events), 100);
        if (n == 0)
                return -TEST_ERROR_TIMEOUT;

        for (long i = 0; i < n; i += 1) {
                if (events[i].data.ptr == test->server)
                        assert(varlink_server_process_events(test->server) == 0);
                else if (events[i].data.ptr == test->connection)
                        assert(varlink_connection_process_events(test->connection, events[i].events) == 0);
                else
                        assert(false);
        }

        return 0;
}

int main(void) {
        const char *interface = "interface org.varlink.example\n"
                                "method Echo(word: string) -> (word: string)\n"
                                "method Later() -> ()";
        const char *words[] = { "one", "two", "three", "four", "five" };

        Test test = { 0 };
        VarlinkCall *later_call = NULL;
        long r;

        assert(varlink_server_new(&test.server,
                                  "unix:test.socket",
                                  -1,
                                  NULL,
                                  &interface, 1) == 0);
        assert(varlink_server_set_method_callback(test.server,
                                                  "org.varlink.example.Echo",
                                                  org_varlink_example_Echo, NULL) == 0);
        assert(varlink_server_set_method_callback(test.server,
                                                  "org.varlink.example.Later",
                                                  org_varlink_example_Later, &later_call) == 0);

        assert(varlink_connection_new(&test.connection, "unix:test.socket") == 0);

        test.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        assert(test.epoll_fd > 0);
        assert(epoll_add(test.epoll_fd,
                         varlink_server_get_fd(test.server),
                         EPOLLIN,
                         test.server) == 0);
        assert(epoll_add(test.epoll_fd,
                         varlink_connection_get_fd(test.connection),
                         varlink_connection_get_events(test.connection),
                         test.connection) == 0);

        {
                unsigned long n_received = 0;

                for (unsigned long i = 0; i < ARRAY_SIZE(words); i += 1) {
                        VarlinkObject *parameters;

                        assert(varlink_object_new(&parameters) == 0);
                        assert(varlink_object_set_string(parameters, "word", words[i]) == 0);
                        assert(varlink_connection_call(test.connection, "org.varlink.example.Echo", parameters, 0) == 0);
                        assert(varlink_object_unref(parameters) == NULL);
                }

                for (long i = 0; n_received < ARRAY_SIZE(words) && i < 10; i += 1) {
                        r = test_process_events(&test);
                        assert(r == 0 || r == -TEST_ERROR_TIMEOUT);

                        for (;;) {
                                VarlinkObject *out;
                                const char *result;

                                assert(varlink_connection_receive_reply(test.connection, &out, NULL, NULL) == 0);
                                if (!out)
                                        break;

                                assert(varlink_object_get_string(out, "word", &result) == 0);
                                assert(strcmp(result, words[n_received]) == 0);
                                assert(varlink_object_unref(out) == NULL);

                                n_received += 1;
                        }
                }
                assert(n_received == ARRAY_SIZE(words));
        }

        {
                VarlinkObject *out = NULL;

                assert(varlink_connection_call(test.connection, "org.varlink.example.Later", NULL, 0) == 0);
                for (long i = 0; later_call == NULL && i < 10; i += 1) {
                        r = test_process_events(&test);
                        assert(r == 0 || r == -TEST_ERROR_TIMEOUT);
                }
                assert(later_call != NULL);

                assert(varlink_call_reply(later_call, NULL, 0) == 0);
                later_call = varlink_call_unref(later_call);
                assert(later_call == NULL);

                for (long i = 0; out == NULL && i < 10; i += 1) {
                        r = test_process_events(&test);
                        assert(r == 0 || r == -TEST_ERROR_TIMEOUT);
                        assert(varlink_connection_receive_reply(test.connection, &out, NULL, NULL) == 0);
                }
                assert(out != NULL);
                assert(varlink_object_unref(out) == NULL);
        }

        assert(varlink_connection_close(test.connection) == NULL);
        assert(varlink_server_free(test.server) == NULL);
        close(test.epoll_fd);

        return EXIT_SUCCESS;
}
