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
        VarlinkConnection *connection;
        int epoll_fd;
} Test;

static long org_varlink_example_Echo(VarlinkService *service,
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

static long org_varlink_example_Later(VarlinkService *service,
                                      VarlinkCall *call,
                                      VarlinkObject *parameters,
                                      uint64_t flags,
                                      void *userdata) {
        VarlinkCall **callp = userdata;
        pid_t pid;
        uid_t uid;
        gid_t gid;

        assert(varlink_call_get_credentials(call, &pid, &uid, &gid) == 0);
        assert(pid == getpid());
        assert(uid == getuid());
        assert(gid == getgid());

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

typedef struct {
        const char **words;
        unsigned long n_received;
} EchoCall;

static void echo_callback(VarlinkConnection *connection,
                          const char *error,
                          VarlinkObject *parameters,
                          uint64_t flags,
                          void *userdata) {
        EchoCall *call = userdata;
        const char *result;

        assert(varlink_object_get_string(parameters, "word", &result) == 0);
        assert(strcmp(result, call->words[call->n_received]) == 0);

        call->n_received += 1;
}

static void later_callback(VarlinkConnection *connection,
                           const char *error,
                           VarlinkObject *parameters,
                           uint64_t flags,
                           void *userdata) {
        VarlinkObject **out = userdata;

        *out = varlink_object_ref(parameters);
}

int main(void) {
        const char *interface = "interface org.varlink.example\n"
                                        "method Echo(word: string) -> (word: string)\n"
                                        "method Later() -> ()";
        const char *words[] = { "one", "two", "three", "four", "five" };

        Test test = { 0 };
        VarlinkCall *later_call = NULL;

        assert(varlink_service_new(&test.service, "Varlink", "Test Service", "0.1", "http://", "@test.socket", -1) == 0);
        assert(varlink_service_add_interface(test.service, interface,
                                             "Echo", org_varlink_example_Echo, NULL,
                                             "Later", org_varlink_example_Later, &later_call,
                                             NULL) == 0);

        assert(varlink_connection_new(&test.connection, "@test.socket") == 0);

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

        {
                EchoCall call = {
                        .words = words,
                        .n_received = 0
                };

                for (unsigned long i = 0; i < ARRAY_SIZE(words); i += 1) {
                        VarlinkObject *parameters;

                        assert(varlink_object_new(&parameters) == 0);
                        assert(varlink_object_set_string(parameters, "word", words[i]) == 0);
                        assert(varlink_connection_call(test.connection, "org.varlink.example.Echo", parameters, 0,
                                                       echo_callback, &call) == 0);
                        assert(varlink_object_unref(parameters) == NULL);
                }

                for (long i = 0; call.n_received < ARRAY_SIZE(words) && i < 10; i += 1)
                        assert(test_process_events(&test) == 0);

                assert(call.n_received == ARRAY_SIZE(words));
        }

        {
                EchoCall call = {
                        .words = words,
                        .n_received = 0
                };

                for (unsigned long i = 0; i < ARRAY_SIZE(words); i += 1) {
                        VarlinkObject *parameters;

                        assert(varlink_object_new(&parameters) == 0);
                        assert(varlink_object_set_string(parameters, "word", words[i]) == 0);
                        assert(varlink_connection_call(test.connection, "org.varlink.example.Echo", parameters, VARLINK_CALL_ONEWAY,
                                                       echo_callback, &call) == 0);
                        assert(varlink_object_unref(parameters) == NULL);
                }

                assert(test_process_events(&test) == 0);

                assert(call.n_received == 0);
        }

        {
                VarlinkObject *out = NULL;

                assert(varlink_connection_call(test.connection, "org.varlink.example.Later", NULL, 0,
                                               later_callback, &out) == 0);
                for (long i = 0; later_call == NULL && i < 10; i += 1)
                        assert(test_process_events(&test) == 0);

                assert(later_call != NULL);

                assert(varlink_call_reply(later_call, NULL, 0) == 0);
                later_call = varlink_call_unref(later_call);
                assert(later_call == NULL);

                for (long i = 0; out == NULL && i < 10; i += 1)
                        assert(test_process_events(&test) == 0);

                assert(out != NULL);
                assert(varlink_object_unref(out) == NULL);
        }

        assert(varlink_connection_free(test.connection) == NULL);
        assert(varlink_service_free(test.service) == NULL);
        close(test.epoll_fd);

        return EXIT_SUCCESS;
}
