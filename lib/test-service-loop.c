// SPDX-License-Identifier: Apache-2.0

#include "varlink.h"
#include "util.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

typedef struct {
        VarlinkService *service;
        VarlinkServiceConnection *accepted;
        VarlinkConnection *connection;
        int epoll_fd;
        bool accepted_closed;
} Test;

static long org_varlink_example_Echo(VarlinkService *UNUSED(service),
                                     VarlinkCall *call,
                                     VarlinkObject *parameters,
                                     uint64_t UNUSED(flags),
                                     void *UNUSED(userdata)) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *out = NULL;
        const char *word;

        assert(varlink_object_get_string(parameters, "word", &word) == 0);

        assert(varlink_object_new(&out) == 0);
        assert(varlink_object_set_string(out, "word", word) == 0);

        return varlink_call_reply(call, out, 0);
}

static long org_varlink_example_Later(VarlinkService *UNUSED(service),
                                      VarlinkCall *call,
                                      VarlinkObject *UNUSED(parameters),
                                      uint64_t UNUSED(flags),
                                      void *userdata) {
        VarlinkCall **callp = userdata;

        assert(*callp == NULL);
        *callp = varlink_call_ref(call);

        return 0;
}

static void on_accepted_closed(VarlinkServiceConnection *UNUSED(connection), void *userdata) {
        bool *closed = userdata;

        *closed = true;
}

static void test_arm(Test *test) {
        assert(epoll_mod(test->epoll_fd,
                         varlink_connection_get_fd(test->connection),
                         varlink_connection_get_events(test->connection),
                         test->connection) == 0);

        if (test->accepted && !varlink_service_connection_is_closed(test->accepted))
                assert(epoll_mod(test->epoll_fd,
                                 varlink_service_connection_get_fd(test->accepted),
                                 varlink_service_connection_get_events(test->accepted),
                                 test->accepted) == 0);
}

static void test_process_events(Test *test) {
        struct epoll_event events[3];
        long n;

        test_arm(test);

        n = epoll_wait(test->epoll_fd, events, ARRAY_SIZE(events), 1000);
        assert(n > 0);

        for (long i = 0; i < n; i += 1) {
                if (events[i].data.ptr == test) {
                        assert(varlink_service_accept(test->service, &test->accepted) == 1);
                        assert(test->accepted);

                        varlink_service_connection_set_closed_callback(test->accepted,
                                                                       on_accepted_closed,
                                                                       &test->accepted_closed);

                        assert(epoll_add(test->epoll_fd,
                                         varlink_service_connection_get_fd(test->accepted),
                                         varlink_service_connection_get_events(test->accepted),
                                         test->accepted) == 0);
                } else if (events[i].data.ptr == test->connection) {
                        varlink_connection_process_events(test->connection, events[i].events);
                } else if (events[i].data.ptr == test->accepted) {
                        varlink_service_connection_process_events(test->accepted, events[i].events);
                } else
                        assert(false);
        }
}

typedef struct {
        const char *word;
        unsigned long n_received;
} EchoCall;

static long echo_callback(VarlinkConnection *UNUSED(connection),
                          const char *UNUSED(error),
                          VarlinkObject *parameters,
                          uint64_t UNUSED(flags),
                          void *userdata) {
        EchoCall *call = userdata;
        const char *result;

        assert(varlink_object_get_string(parameters, "word", &result) == 0);
        assert(strcmp(result, call->word) == 0);

        call->n_received += 1;
        return 0;
}

static long later_callback(VarlinkConnection *UNUSED(connection),
                           const char *UNUSED(error),
                           VarlinkObject *UNUSED(parameters),
                           uint64_t UNUSED(flags),
                           void *userdata) {
        unsigned long *n_received = userdata;

        *n_received += 1;
        return 0;
}

static void call_echo(Test *test, const char *word, EchoCall *out) {
        _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;

        *out = (EchoCall) { .word = word, .n_received = 0 };

        assert(varlink_object_new(&parameters) == 0);
        assert(varlink_object_set_string(parameters, "word", word) == 0);
        assert(varlink_connection_call(test->connection, "org.varlink.example.Echo",
                                       parameters, 0, echo_callback, out) == 0);
}

static void run_until(Test *test, unsigned long *counter, unsigned long target) {
        for (long i = 0; *counter < target && i < 10; i += 1)
                test_process_events(test);

        assert(*counter == target);
}

int main(void) {
        const char *interface = "interface org.varlink.example\n"
                                        "method Echo(word: string) -> (word: string)\n"
                                        "method Later() -> ()";
        Test test = {};
        VarlinkCall *later_call = NULL;

        assert(varlink_service_new(&test.service,
                                   "Varlink", "Test Service", "1", "http://example.com",
                                   "unix:@test-service-loop.socket",
                                   -1) == 0);
        assert(varlink_service_add_interface(test.service, interface,
                                             "Echo", org_varlink_example_Echo, NULL,
                                             "Later", org_varlink_example_Later, &later_call,
                                             NULL) == 0);

        /* The internal epoll descriptor is gone once the caller owns the loop */
        assert(varlink_service_set_external_loop(test.service, true) == 0);
        assert(varlink_service_get_fd(test.service) == -VARLINK_ERROR_INVALID_CALL);
        assert(varlink_service_process_events(test.service) == -VARLINK_ERROR_INVALID_CALL);
        assert(varlink_service_get_listen_fd(test.service) >= 0);

        test.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        assert(test.epoll_fd > 0);
        assert(epoll_add(test.epoll_fd,
                         varlink_service_get_listen_fd(test.service),
                         EPOLLIN,
                         &test) == 0);

        assert(varlink_connection_new(&test.connection, "unix:@test-service-loop.socket") == 0);
        assert(epoll_add(test.epoll_fd,
                         varlink_connection_get_fd(test.connection),
                         varlink_connection_get_events(test.connection),
                         test.connection) == 0);

        /* A call answered from the method callback */
        {
                EchoCall call;

                call_echo(&test, "one", &call);
                run_until(&test, &call.n_received, 1);
        }

        assert(test.accepted);
        assert(!varlink_service_connection_is_closed(test.accepted));

        /* A call answered after its dispatch returned, followed by another one
         * on the same connection */
        {
                unsigned long n_later = 0;
                EchoCall call;

                assert(varlink_connection_call(test.connection, "org.varlink.example.Later",
                                               NULL, 0, later_callback, &n_later) == 0);

                for (long i = 0; later_call == NULL && i < 10; i += 1)
                        test_process_events(&test);
                assert(later_call);

                assert(varlink_call_reply(later_call, NULL, 0) == 0);
                later_call = varlink_call_unref(later_call);

                run_until(&test, &n_later, 1);

                call_echo(&test, "two", &call);
                run_until(&test, &call.n_received, 1);
        }

        /* A deferred call pipelined with a second one, which is already in the
         * read buffer by the time the first is answered */
        {
                unsigned long n_later = 0;
                EchoCall call = { .word = "three", .n_received = 0 };
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameters = NULL;

                assert(varlink_object_new(&parameters) == 0);
                assert(varlink_object_set_string(parameters, "word", "three") == 0);

                assert(varlink_connection_call(test.connection, "org.varlink.example.Later",
                                               NULL, 0, later_callback, &n_later) == 0);
                assert(varlink_connection_call(test.connection, "org.varlink.example.Echo",
                                               parameters, 0, echo_callback, &call) == 0);

                for (long i = 0; later_call == NULL && i < 10; i += 1)
                        test_process_events(&test);
                assert(later_call);

                assert(varlink_call_reply(later_call, NULL, 0) == 0);
                later_call = varlink_call_unref(later_call);

                run_until(&test, &n_later, 1);
                run_until(&test, &call.n_received, 1);
        }

        /* Answering a call whose connection is gone reports it instead of
         * following the connection to freed memory */
        {
                unsigned long n_later = 0;

                assert(varlink_connection_call(test.connection, "org.varlink.example.Later",
                                               NULL, 0, later_callback, &n_later) == 0);

                for (long i = 0; later_call == NULL && i < 10; i += 1)
                        test_process_events(&test);
                assert(later_call);

                assert(varlink_service_connection_close(test.accepted) == 0);
                assert(test.accepted_closed);
                assert(varlink_service_connection_is_closed(test.accepted));

                assert(varlink_call_reply(later_call, NULL, 0) == -VARLINK_ERROR_CONNECTION_CLOSED);
                later_call = varlink_call_unref(later_call);
        }

        assert(varlink_service_connection_unref(test.accepted) == NULL);
        assert(varlink_connection_free(test.connection) == NULL);
        assert(varlink_service_free(test.service) == NULL);
        close(test.epoll_fd);

        return EXIT_SUCCESS;
}
