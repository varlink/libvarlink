#include "scanner.h"
#include "util.h"
#include "parse-error.h"
#include "varlink.h"

#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct Scanner {
        const char *string;
        const char *p;
        const char *pline;
        unsigned long line_nr;

        const char *last_comment_start;
        bool json;

        VarlinkParseError *error;
};

/* Names must not be subject to libc's locale rules. */
static inline bool ascii_is_lower(char c) {
        return c >= 'a' && c <= 'z';
}

static inline bool ascii_is_upper(char c) {
        return c >= 'A' && c <= 'Z';
}

static inline bool ascii_is_alpha(char c) {
        return ascii_is_lower(c) || ascii_is_upper(c);
}

static inline bool ascii_is_digit(char c) {
        return c >= '0' && c <= '9';
}

long scanner_new_interface(Scanner **scannerp, const char *string) {
        Scanner *scanner;

        scanner = calloc(1, sizeof(Scanner));
        scanner->string = string;
        scanner->p = scanner->string;
        scanner->pline = scanner->string;
        scanner->line_nr = 1;

        *scannerp = scanner;

        return 0;
}

long scanner_new_json(Scanner **scannerp, const char *string) {
        Scanner *scanner;

        scanner = calloc(1, sizeof(Scanner));
        scanner->string = string;
        scanner->p = scanner->string;
        scanner->pline = scanner->string;
        scanner->line_nr = 1;
        scanner->json = true;

        *scannerp = scanner;

        return 0;
}

void scanner_free(Scanner *scanner) {
        if (scanner->error)
                varlink_parse_error_free(scanner->error);

        free(scanner);
}

void scanner_freep(Scanner **scannerp) {
        if (*scannerp)
                scanner_free(*scannerp);
}

bool scanner_error(Scanner *scanner, const char *fmt, ...) {
        if (!scanner->error) {
                va_list ap;

                varlink_parse_error_new(&scanner->error);
                scanner->error->line_nr = scanner->line_nr;
                scanner->error->pos_nr = 1 + scanner->p - scanner->pline;

                va_start(ap, fmt);
                vasprintf(&scanner->error->message, fmt, ap);
                va_end(ap);
        }

        return false;
}

void scanner_steal_error(Scanner *scanner, VarlinkParseError **errorp) {
        if (errorp) {
                *errorp = scanner->error;
                scanner->error = NULL;
        }
}

static const char *scanner_advance(Scanner *scanner) {
        for (;;) {
                switch (*scanner->p) {
                        case ' ':
                        case '\t':
                                scanner->p += 1;
                                break;

                        case '\n':
                                if (scanner->pline == scanner->p)
                                        scanner->last_comment_start = NULL;

                                scanner->p += 1;
                                scanner->pline = scanner->p;
                                scanner->line_nr += 1;
                                break;

                        case '#':
                                if (scanner->json)
                                        return scanner->p;

                                if (!scanner->last_comment_start)
                                        scanner->last_comment_start = scanner->p;

                                scanner->p = strchrnul(scanner->p, '\n');
                                break;

                        default:
                                return scanner->p;
                }
        }
}

char *scanner_get_last_docstring(Scanner *scanner) {
        FILE *stream = NULL;
        char *docstring = NULL;
        unsigned long size;
        const char *p;

        scanner_advance(scanner);

        if (!scanner->last_comment_start)
                return NULL;

        stream = open_memstream(&docstring, &size);

        p = scanner->last_comment_start;
        while (*p == '#') {
                const char *start = p + 1;

                if (*start == ' ')
                        start += 1;

                p = strchrnul(start, '\n');
                p += 1;

                fwrite(start, 1, p - start, stream);

                /* Skip all leading whitespace in the next line */
                while (*p == ' ' || *p == '\t')
                        p += 1;
        }

        fclose(stream);

        scanner->last_comment_start = NULL;

        return docstring;
}

char scanner_peek(Scanner *scanner) {
        scanner_advance(scanner);

        return *scanner->p;
}

bool scanner_read_keyword(Scanner *scanner, const char *keyword) {
        unsigned long len = strlen(keyword);
        char c;

        scanner_advance(scanner);

        if (strncmp(scanner->p, keyword, len) != 0)
                return false;

        c = scanner->p[len];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c == '_'))
                return false;

        scanner->p += len;

        return true;
}

bool scanner_read_identifier(Scanner *scanner, bool (*is_allowed_char)(char c, bool first), char **identifierp) {
        const char *p;

        p = scanner_advance(scanner);

        if (*p == '\0' || !is_allowed_char(*p, true))
                return false;

        p += 1;

        while (*p != '\0' && is_allowed_char(*p, false))
                p += 1;

        if (identifierp)
                *identifierp = strndup(scanner->p, p - scanner->p);

        scanner->p = p;

        return true;
}

bool scanner_expect_identifier(Scanner *scanner, bool (*allow)(char c, bool first), char **identifierp) {
        if (!scanner_read_identifier(scanner, allow, identifierp))
                return scanner_error(scanner, "identifier expected");

        return true;
}

static bool unhex(char d, uint8_t *valuep) {
        switch (d) {
                case '0' ... '9':
                        *valuep = d - '0';
                        return true;

                case 'a' ... 'f':
                        *valuep = d - 'a' + 0x0a;
                        return true;

                case 'A' ... 'F':
                        *valuep = d - 'A' + 0x0a;
                        return true;

                default:
                        return false;
        }
}

static bool read_unicode_char(const char *p, FILE *stream) {
        uint8_t digits[4];
        uint16_t cp;

        for (unsigned long i = 0; i < 4; i += 1)
                if (p[i] == '\0' || !unhex(p[i], &digits[i]))
                        return false;

        cp = digits[0] << 12 | digits[1] << 8 | digits[2] << 4 | digits[3];

        if (cp <= 0x007f) {
                fprintf(stream, "%c", (char)cp);

        } else if (cp <= 0x07ff) {
                fprintf(stream, "%c", (char)(0xc0 | (cp >> 6)));
                fprintf(stream, "%c", (char)(0x80 | (cp & 0x3f)));
        }

        else {
                fprintf(stream, "%c", (char)(0xe0 | (cp >> 12)));
                fprintf(stream, "%c", (char)(0x80 | ((cp >> 6) & 0x3f)));
                fprintf(stream, "%c", (char)(0x80 | (cp & 0x3f)));
        }

        return true;
}


bool scanner_expect_json_string(Scanner *scanner, char **stringp) {
        _cleanup_(freep) char *string = NULL;
        _cleanup_(fclosep) FILE *stream = NULL;
        unsigned long size;
        const char *p;

        p = scanner_advance(scanner);

        if (*p != '"')
                return false;

        p += 1;

        stream = open_memstream(&string, &size);

        for (;;) {
                if (*p == '\0')
                        return false;

                if (*p == '"') {
                        p += 1;
                        break;
                }

                if (*p == '\\') {
                        p += 1;
                        switch (*p) {
                                case '"':
                                        fprintf(stream, "\"");
                                        break;

                                case '\\':
                                        fprintf(stream, "\\");
                                        break;

                                case '/':
                                        fprintf(stream, "/");
                                        break;

                                case 'b':
                                        fprintf(stream, "\b");
                                        break;

                                case 'f':
                                        fprintf(stream, "\f");
                                        break;

                                case 'n':
                                        fprintf(stream, "\n");
                                        break;

                                case 'r':
                                        fprintf(stream, "\r");
                                        break;

                                case 't':
                                        fprintf(stream, "\t");
                                        break;

                                case 'u':
                                        if (!read_unicode_char(p + 1, stream))
                                                return scanner_error(scanner, "invalid unicode character");

                                        p += 4;
                                        break;

                                default:
                                        return scanner_error(scanner, "invalid escape sequence");
                        }
                } else
                        fprintf(stream, "%c", *p);

                p += 1;
        }

        fclose(stream);
        stream = NULL;

        if (stringp) {
                *stringp = string;
                string = NULL;
        }

        scanner->p = p;

        return true;
}

bool scanner_read_number(Scanner *scanner, ScannerNumber *numberp) {
        ScannerNumber number = { 0 };
        char *end;

        scanner_advance(scanner);

        number.i = strtol(scanner->p, &end, 10);
        if (end == scanner->p)
                return false;

        if (*end == '.' || *end == 'e' || *end == 'E') {
                locale_t loc;

                loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t) 0);

                number.is_double = true;
                number.d = strtod(scanner->p, &end);

                freelocale(loc);
        }

        scanner->p = end;

        if (numberp)
                *numberp = number;

        return true;
}

bool scanner_read_uint(Scanner *scanner, uint64_t *uintp) {
        uint64_t u;

        scanner_advance(scanner);

        /*
         * Don't allow leading 0s and negative values (strtoul converts
         * those to positives)
         */
        if (*scanner->p < '1' || *scanner->p > '9')
                return false;

        u = strtoul(scanner->p, (char **)&scanner->p, 10);

        if (uintp)
                *uintp = u;

        return true;
}

bool scanner_expect_operator(Scanner *scanner, const char *op) {
        unsigned long length = strlen(op);

        scanner_advance(scanner);

        if (strncmp(scanner->p, op, length) != 0)
                return scanner_error(scanner, "'%s' expected", op);

        scanner->p += length;

        return true;
}
