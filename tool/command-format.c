#include "command.h"
#include "interface.h"
#include "parse-error.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

static long read_file(FILE *file, char **contentsp) {
        _cleanup_(freep) char *contents = NULL;
        unsigned long alloced = 0;
        unsigned long size = 0;

        while (!feof(file)) {
                long n;

                if (size == alloced) {
                        alloced = MAX(alloced * 2, 4096);
                        contents = realloc(contents, alloced);
                }

                n = fread(contents + size, 1, alloced - size, file);
                if (n == 0)
                        return -ferror(file);

                size += n;
        }

        *contentsp = contents;
        contents = NULL;

        return 0;
}

static long format(VarlinkCli *cli, int argc, char **argv) {
        static const struct option options[] = {
                { "help",   no_argument,       NULL, 'h' },
                { "output", required_argument, NULL, 'o' },
                {}
        };
        int c;
        const char *in_filename = NULL;
        const char *out_filename = NULL;
        _cleanup_(varlink_interface_freep) VarlinkInterface *interface = NULL;
        _cleanup_(varlink_parse_error_freep) VarlinkParseError *error = NULL;
        _cleanup_(fclosep) FILE *in_file = NULL;
        _cleanup_(fclosep) FILE *out_file = NULL;
        _cleanup_(freep) char *in = NULL;
        _cleanup_(freep) char *out = NULL;
        long r;

        while ((c = getopt_long(argc, argv, "ho:", options, NULL)) >= 0) {
                switch (c) {
                        case 'h':
                                printf("Usage: %s format [OPTIONS]... ARGUMENTS\n", program_invocation_short_name);
                                printf("\n");
                                printf("Format a varlink service file.\n");
                                printf("\n");
                                printf("  -h, --help             display this help text and exit\n");
                                printf("  -o, --output=FILENAME  output to FILENAME instead of stdout\n");
                                return EXIT_SUCCESS;

                        case 'o':
                                out_filename = optarg;
                                break;

                        default:
                                fprintf(stderr, "Try '%s --help' for more information\n",
                                        program_invocation_short_name);
                                return EXIT_FAILURE;
                }
        }

        in_filename = argv[optind];
        if (!in_filename) {
                fprintf(stderr, "Usage: %s [OPTIONS]... FILE\n", program_invocation_short_name);
                return EXIT_FAILURE;
        }

        if (strcmp(in_filename, "-") != 0) {
                in_file = fopen(in_filename, "r");
                if (!in_file) {
                        fprintf(stderr, "Error opening %s for reading: %s\n", in_filename, strerror(errno));
                        return EXIT_FAILURE;
                }
        } else {
                in_file = stdin;
        }

        r = read_file(in_file, &in);
        if (r < 0) {
                fprintf(stderr, "Error reading %s: %s\n", in_filename, strerror(errno));
                return EXIT_FAILURE;
        }

        r = varlink_interface_new(&interface, in, &error);
        if (r < 0) {
                unsigned long line_nr, pos_nr;
                const char *message = varlink_parse_error_get_string(error, &line_nr, &pos_nr);

                fprintf(stderr, "%s:%lu:%lu: %s\n", in_filename, line_nr, pos_nr, message);
                return EXIT_FAILURE;
        }

        r = varlink_interface_write_interfacestring(interface, &out,
                                                    0, 72,
                                                    NULL, NULL,
                                                    NULL, NULL,
                                                    NULL, NULL,
                                                    NULL, NULL);
        if (r < 0) {
                fprintf(stderr, "Error writing interface: %s", strerror(-r));
                return EXIT_FAILURE;
        }

        if (out_filename && strcmp(out_filename, "-") != 0) {
                out_file = fopen(out_filename, "w");
                if (!out_file) {
                        fprintf(stderr, "Error opening %s for writing: %s\n", out_filename, strerror(errno));
                        return EXIT_FAILURE;
                }
        } else {
                out_file = stdout;
        }

        r = fwrite(out, 1, strlen(out), out_file);
        if (r == 0) {
                fprintf(stderr, "Error writing interface: %s\n", strerror(-r));
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

const Command command_format = {
        .name = "format",
        .info = "Format a varlink service file",
        .function = format
};
