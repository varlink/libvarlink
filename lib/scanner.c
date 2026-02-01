// SPDX-License-Identifier: Apache-2.0

#include "scanner.h"
#include "util.h"
#include "varlink.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#ifdef __FreeBSD__
#include <xlocale.h>
#endif
#include "c-utf8.h"

static const char *error_strings[] = {
        [SCANNER_ERROR_PANIC] = "Panic",
        [SCANNER_ERROR_INTERFACE_KEYWORD_EXPECTED] = "InterfaceKeywordExpected",
        [SCANNER_ERROR_KEYWORD_EXPECTED] = "KeywordExpected",
        [SCANNER_ERROR_DUPLICATE_FIELD_NAME] = "DuplicateFieldName",
        [SCANNER_ERROR_INTERFACE_NAME_INVALID] = "InterfaceNameInvalid",
        [SCANNER_ERROR_OBJECT_EXPECTED] = "ObjectExpected",
        [SCANNER_ERROR_DUPLICATE_MEMBER_NAME] = "DuplicateMemberName",
        [SCANNER_ERROR_MEMBER_NAME_INVALID] = "MemberNameInvalid",
        [SCANNER_ERROR_UNKNOWN_TYPE] = "UnknownType",
        [SCANNER_ERROR_FIELD_NAME_INVALID] = "FieldNameInvalid",
        [SCANNER_ERROR_TYPE_NAME_INVALID] = "TypeNameInvalid",
        [SCANNER_ERROR_INVALID_CHARACTER] = "InvalidCharacter",
        [SCANNER_ERROR_OPERATOR_EXPECTED] = "OperatorExpected",
        [SCANNER_ERROR_TYPE_EXPECTED] = "TypeExpected",
        [SCANNER_ERROR_JSON_EXPECTED] = "JsonExpected",
};

const char *scanner_error_string(long error) {
        if (error == 0 || error >= (long)ARRAY_SIZE(error_strings))
                return "<invalid>";

        if (!error_strings[error])
                return "<missing>";

        return error_strings[error];
}

void scanner_error(Scanner *scanner, long error) {
        if (scanner->error.no == 0) {
                scanner->error.no = error;
                scanner->error.line_nr = scanner->line_nr;
                scanner->error.pos_nr = 1 + scanner->p - scanner->pline;
        }
}

long scanner_new(Scanner **scannerp, const char *string, bool comments) {
        Scanner *scanner;

        scanner = calloc(1, sizeof(Scanner));
        if (!scanner)
                return -VARLINK_ERROR_PANIC;

        scanner->string = string;
        scanner->p = scanner->string;
        scanner->pline = scanner->string;
        scanner->line_nr = 1;
        scanner->comments = comments;

        *scannerp = scanner;
        return 0;
}

Scanner *scanner_free(Scanner *scanner) {
        free(scanner);
        return NULL;
}

void scanner_freep(Scanner **scannerp) {
        if (*scannerp)
                scanner_free(*scannerp);
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
                                if (!scanner->comments)
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

long scanner_get_last_docstring(Scanner *scanner, char **stringp) {
        _cleanup_(fclosep) FILE *stream = NULL;
        _cleanup_(freep) char *docstring = NULL;
        size_t size;
        const char *p;

        scanner_advance(scanner);

        if (!scanner->last_comment_start) {
                *stringp = NULL;
                return false;
        }

        stream = open_memstream(&docstring, &size);
        if (!stream)
                return -VARLINK_ERROR_PANIC;

        p = scanner->last_comment_start;
        while (*p == '#') {
                const char *start = p + 1;

                if (*start == ' ')
                        start += 1;

                p = strchrnul(start, '\n');
                p += 1;

                if (fwrite(start, 1, p - start, stream) != (size_t)(p - start))
                        return -VARLINK_ERROR_PANIC;

                /* Skip all leading whitespace in the next line */
                while (*p == ' ' || *p == '\t')
                        p += 1;
        }

        fclose(stream);
        stream = NULL;

        scanner->last_comment_start = NULL;
        *stringp = docstring;
        docstring = NULL;

        return true;
}

char scanner_peek(Scanner *scanner) {
        scanner_advance(scanner);
        return *scanner->p;
}

static unsigned long scanner_word_len(Scanner *scanner) {
        scanner_advance(scanner);

        switch (*scanner->p) {
                case 'a' ... 'z':
                case 'A' ... 'Z':
                        break;

                default:
                        return 0;
        }

        for (unsigned long i = 1;; i += 1) {
                switch (scanner->p[i]) {
                        case '0' ... '9':
                        case 'a' ... 'z':
                        case 'A' ... 'Z':
                        case '_':
                        case '-':
                        case '.':
                                break;

                        default:
                                return i;
                }
        }
}

bool scanner_read_keyword(Scanner *scanner, const char *keyword) {
        unsigned long word_len = scanner_word_len(scanner);
        unsigned long keyword_len = strlen(keyword);

        if (word_len != keyword_len)
                return false;

        if (strncmp(scanner->p, keyword, word_len) != 0)
                return false;

        scanner->p += word_len;

        return true;
}

static bool interface_name_valid(const char *name, unsigned long len) {
        char previous = 0;
        unsigned sections = 1;

        if (len < 3 || len > 255)
                return false;

        /* Only ASCII characters are allowed. */
        for (unsigned long i = 0; i < len; i += 1) {
                switch (name[i]) {
                        case 'a' ... 'z':
                        case 'A' ... 'Z':
                        case '0' ... '9':
                                break;

                        case '-':
                                /* No dashes after a dot. */
                                if (previous == '.')
                                        return false;
                                break;

                        case '.':
                                /* No double dots and no dashes before a dot. */
                                if (previous == '.' || previous == '-')
                                        return false;
                                sections += 1;
                                break;

                        default:
                                return false;
                }

                previous = name[i];
        }

        /* The top-level element starts with an alpha character. */
        switch (name[0]) {
                case 'a' ... 'z':
                case 'A' ... 'Z':
                        break;

                default:
                        return false;
        }

        /* At least two elements are required. */
        if (sections < 2)
                return false;

        /* The last element ends alphanumeric. */
        switch (name[len - 1]) {
                case 'a' ... 'z':
                case 'A' ... 'Z':
                case '0' ... '9':
                        break;

                default:
                        return false;
        }

        return true;
}

long scanner_expect_interface_name(Scanner *scanner, char **namep) {
        unsigned long len = scanner_word_len(scanner);
        char *name;

        if (!interface_name_valid(scanner->p, len)) {
                scanner_error(scanner, SCANNER_ERROR_INTERFACE_NAME_INVALID);
                return -VARLINK_ERROR_INVALID_INTERFACE;
        }

        name = strndup(scanner->p, len);
        if (!name)
                return -VARLINK_ERROR_PANIC;

        *namep = name;
        scanner->p += len;

        return 0;
}

long scanner_expect_field_name(Scanner *scanner, char **namep) {
        unsigned long len = scanner_word_len(scanner);
        char *name;

        switch (*scanner->p) {
                case 'a' ... 'z':
                case 'A' ... 'Z':
                        break;

                default:
                        scanner_error(scanner, SCANNER_ERROR_FIELD_NAME_INVALID);
                        return -VARLINK_ERROR_INVALID_TYPE;
        }

        for (unsigned long i = 1; i < len; i += 1) {
                switch (scanner->p[i]) {
                        case '0' ... '9':
                        case 'a' ... 'z':
                        case 'A' ... 'Z':
                                break;

                        case '_':
                                if (scanner->p[i - 1] == '_') {
                                        scanner_error(scanner, SCANNER_ERROR_FIELD_NAME_INVALID);
                                        return -VARLINK_ERROR_INVALID_TYPE;
                                }
                                break;

                        default:
                                scanner_error(scanner, SCANNER_ERROR_FIELD_NAME_INVALID);
                                return -VARLINK_ERROR_INVALID_TYPE;
                }
        }

        switch (scanner->p[len - 1]) {
                case '0' ... '9':
                case 'a' ... 'z':
                case 'A' ... 'Z':
                        break;

                default:
                        scanner_error(scanner, SCANNER_ERROR_FIELD_NAME_INVALID);
                        return -VARLINK_ERROR_INVALID_TYPE;
        }

        name = strndup(scanner->p, len);
        if (!name)
                return -VARLINK_ERROR_PANIC;

        *namep = name;
        scanner->p += len;

        return 0;
}

static bool member_name_valid(const char *member, unsigned long len) {
        switch (*member) {
                case 'A' ... 'Z':
                        break;

                default:
                        return false;
        }

        for (unsigned long i = 1; i < len; i += 1) {
                switch (member[i]) {
                        case '0' ... '9':
                        case 'a' ... 'z':
                        case 'A' ... 'Z':
                                break;

                        default:
                                return false;
                }
        }

        return true;
}

long scanner_expect_member_name(Scanner *scanner, char **namep) {
        unsigned long len = scanner_word_len(scanner);
        char *name;

        if (!member_name_valid(scanner->p, len)) {
                scanner_error(scanner, SCANNER_ERROR_MEMBER_NAME_INVALID);
                return -VARLINK_ERROR_INVALID_IDENTIFIER;
        }

        name = strndup(scanner->p, len);
        if (!name)
                return -VARLINK_ERROR_PANIC;

        *namep = name;
        scanner->p += len;

        return 0;
}

long scanner_expect_type_name(Scanner *scanner, char **namep) {
        unsigned long len = scanner_word_len(scanner);
        unsigned long interface_len = 0;
        const char *member = NULL;
        unsigned long member_len = 0;
        char *name;

        if (member_name_valid(scanner->p, len)) {
                name = strndup(scanner->p, len);
                if (!name)
                        return -VARLINK_ERROR_PANIC;

                *namep = name;
                scanner->p += len;

                return 0;
        }

        if (len < 3) {
                scanner_error(scanner, SCANNER_ERROR_TYPE_NAME_INVALID);
                return -VARLINK_ERROR_INVALID_IDENTIFIER;
        }

        for (unsigned long i = 0; i < len; i += 1) {
                if (scanner->p[i] >= 'A' && scanner->p[i] <= 'Z') {
                        if (scanner->p[i - 1] != '.') {
                                scanner_error(scanner, SCANNER_ERROR_TYPE_NAME_INVALID);
                                return -VARLINK_ERROR_INVALID_IDENTIFIER;
                        }

                        interface_len = i - 1;
                        member = scanner->p + i;
                        member_len = len - i;
                        break;
                }
        }

        if (!interface_name_valid(scanner->p, interface_len)) {
                scanner_error(scanner, SCANNER_ERROR_TYPE_NAME_INVALID);
                return -VARLINK_ERROR_INVALID_IDENTIFIER;
        }

        if (!member_name_valid(member, member_len)) {
                scanner_error(scanner, SCANNER_ERROR_TYPE_NAME_INVALID);
                return -VARLINK_ERROR_INVALID_IDENTIFIER;
        }

        name = strndup(scanner->p, len);
        if (!name)
                return -VARLINK_ERROR_PANIC;

        *namep = name;
        scanner->p += len;

        return 0;
}

static bool unhex(char d, uint8_t *valuep) {
        switch (d) {
                case '0' ... '9':
                        *valuep = (uint8_t) (d - '0');
                        return true;

                case 'a' ... 'f':
                        *valuep = (uint8_t) (d - 'a' + 0x0a);
                        return true;

                case 'A' ... 'F':
                        *valuep = (uint8_t) (d - 'A' + 0x0a);
                        return true;

                default:
                        return false;
        }
}

static size_t read_unicode_char(const char *p, FILE *stream) {
        uint8_t digits[4];
        uint32_t cp;
        uint16_t cu;
        size_t size = 4;

        for (unsigned long i = 0; i < 4; i += 1)
                if (p[i] == '\0' || !unhex(p[i], &digits[i]))
                        return 0;

        cu = digits[0] << 12 | digits[1] << 8 | digits[2] << 4 | digits[3];

        switch (cu) {
                case 0xD800 ... 0xDBFF:
                        cp = 0x10000 + ((cu - 0xD800) << 10);

                        if (p[4] != '\\' || p[5] != 'u')
                                return 0;

                        for (unsigned long i = 0; i < 4; i += 1)
                                if (p[i + 6] == '\0' || !unhex(p[i + 6], &digits[i]))
                                        return 0;

                        cu = digits[0] << 12 | digits[1] << 8 | digits[2] << 4 | digits[3];

                        size = 10;

                        if (cu < 0xDC00 || cu > 0xDFFF)
                                return 0;

                        cp += cu - 0xDC00;

                        break;
                case 0xDC00 ... 0xDFFF:
                        return 0;
                default:
                        cp = cu;
                        break;
        }

        if (cp <= 0x007f) {
                fprintf(stream, "%c", (char)cp);

        } else if (cp <= 0x07ff) {
                fprintf(stream, "%c", (char)(0xc0 | (cp >> 6)));
                fprintf(stream, "%c", (char)(0x80 | (cp & 0x3f)));
        }

        else if (cp >= 0x0800 && cp <= 0xFFFF) {
                fprintf(stream, "%c", (char)(0xe0 | (cp >> 12)));
                fprintf(stream, "%c", (char)(0x80 | ((cp >> 6) & 0x3f)));
                fprintf(stream, "%c", (char)(0x80 | (cp & 0x3f)));
        }

        else if (cp >= 0x10000 && cp <= 0x10FFFF) {
                fprintf(stream, "%c", (char)(0xf0 | (cp >> 18)));
                fprintf(stream, "%c", (char)(0x80 | ((cp >> 12) & 0x3f)));
                fprintf(stream, "%c", (char)(0x80 | ((cp >> 6) & 0x3f)));
                fprintf(stream, "%c", (char)(0x80 | (cp & 0x3f)));
        }

        return size;
}


long scanner_expect_string(Scanner *scanner, char **stringp) {
        _cleanup_(freep) char *string = NULL;
        _cleanup_(fclosep) FILE *stream = NULL;
        size_t size, utf8_len;
        const char *p;
        const char *utf8_str;

        p = scanner_advance(scanner);

        if (*p != '"')
                return -VARLINK_ERROR_INVALID_JSON;

        p += 1;

        stream = open_memstream(&string, &size);
        if (!stream)
                return -VARLINK_ERROR_PANIC;

        for (;;) {
                if (*p == '\0')
                        return -VARLINK_ERROR_INVALID_JSON;

                if (*p == '\t')
                        return -VARLINK_ERROR_INVALID_JSON;

                if (*p == '\n')
                        return -VARLINK_ERROR_INVALID_JSON;

                if (*p == '"') {
                        p += 1;
                        break;
                }

                if (*p == '\\') {
                        p += 1;
                        switch (*p) {
                                case '"':
                                        if (fprintf(stream, "\"") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case '\\':
                                        if (fprintf(stream, "\\") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case '/':
                                        if (fprintf(stream, "/") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case 'b':
                                        if (fprintf(stream, "\b") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case 'f':
                                        if (fprintf(stream, "\f") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case 'n':
                                        if (fprintf(stream, "\n") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case 'r':
                                        if (fprintf(stream, "\r") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case 't':
                                        if (fprintf(stream, "\t") < 0)
                                                return -VARLINK_ERROR_PANIC;
                                        break;

                                case 'u':
                                        size =read_unicode_char(p + 1, stream);
                                        if ( size == 0) {
                                                scanner_error(scanner, SCANNER_ERROR_INVALID_CHARACTER);
                                                return -VARLINK_ERROR_INVALID_JSON;
                                        }

                                        p += size;
                                        break;

                                default:
                                        scanner_error(scanner, SCANNER_ERROR_INVALID_CHARACTER);
                                        return -VARLINK_ERROR_INVALID_JSON;
                        }

                } else if (fprintf(stream, "%c", *p) < 0)
                        return -VARLINK_ERROR_PANIC;

                p += 1;
        }

        fclose(stream);
        stream = NULL;

        utf8_str = string;
        utf8_len = size;
        c_utf8_verify(&utf8_str, &utf8_len);
        if (utf8_len != 0) {
                scanner_error(scanner, SCANNER_ERROR_INVALID_CHARACTER);
                return -VARLINK_ERROR_INVALID_JSON;
        }

        if (stringp) {
                *stringp = string;
                string = NULL;
        }

        scanner->p = p;
        return 0;
}

bool scanner_read_number(Scanner *scanner, ScannerNumber *numberp, locale_t locale) {
        ScannerNumber number = {};
        char *end;

        scanner_advance(scanner);

        number.i = strtol(scanner->p, &end, 10);
        if (end == scanner->p)
                return false;

        if (*end == '.' || *end == 'e' || *end == 'E') {
                number.is_double = true;
                number.d = strtod_l(scanner->p, &end, locale);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
                // Exactly `±HUGE_VAL` is returned on ERANGE
                if ((errno == ERANGE) && (number.d == -HUGE_VAL || number.d == HUGE_VAL))
                        return false;
#pragma GCC diagnostic pop
        } else {
                if ((errno == ERANGE) && (number.i == LONG_MIN || number.i == LONG_MAX))
                        return false;
        }

        scanner->p = end;

        if (numberp)
                *numberp = number;

        return true;
}

long scanner_expect_operator(Scanner *scanner, const char *op) {
        unsigned long length = strlen(op);

        scanner_advance(scanner);

        if (strncmp(scanner->p, op, length) != 0) {
                scanner_error(scanner, SCANNER_ERROR_OPERATOR_EXPECTED);
                return -VARLINK_ERROR_INVALID_IDENTIFIER;
        }

        scanner->p += length;
        return 0;
}
