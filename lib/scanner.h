#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
        SCANNER_ERROR_PANIC = 1,
        SCANNER_ERROR_INTERFACE_KEYWORD_EXPECTED,
        SCANNER_ERROR_KEYWORD_EXPECTED,
        SCANNER_ERROR_DUPLICATE_FIELD_NAME,
        SCANNER_ERROR_INTERFACE_NAME_INVALID,
        SCANNER_ERROR_OBJECT_EXPECTED,
        SCANNER_ERROR_DUPLICATE_MEMBER_NAME,
        SCANNER_ERROR_MEMBER_NAME_INVALID,
        SCANNER_ERROR_UNKNOWN_TYPE,
        SCANNER_ERROR_FIELD_NAME_INVALID,
        SCANNER_ERROR_TYPE_NAME_INVALID,
        SCANNER_ERROR_INVALID_CHARACTER,
        SCANNER_ERROR_OPERATOR_EXPECTED,
        SCANNER_ERROR_TYPE_EXPECTED,
        SCANNER_ERROR_JSON_EXPECTED,
        SCANNER_ERROR_MAX
};

typedef struct {
        const char *string;
        const char *p;
        const char *pline;
        unsigned long line_nr;

        bool comments;
        const char *last_comment_start;

        struct {
                long no;
                unsigned long line_nr;
                unsigned long pos_nr;
        } error;
} Scanner;

typedef struct {
        bool is_double;
        union {
                double d;
                int64_t i;
        };
} ScannerNumber;

void scanner_error(Scanner *scanner, long error);
const char *scanner_error_string(long error);

long scanner_new(Scanner **scannerp, const char *string, bool comments);
Scanner *scanner_free(Scanner *scanner);
void scanner_freep(Scanner **scannerp);

/*
 * If the scanner was created with scanner_new_interface(), return the
 * last docstring that the scanner encountered (a multi-line comment
 * that starts at the beginning of a line and immediately preceded a
 * token). Returns false if the scanner wasn't advanced or no docstring
 * was found since the last call to this function.
 */
long scanner_get_last_docstring(Scanner *scanner, char **stringp);

/*
 * Advances the scanner and returns the first character of the next
 * token.
 */
char scanner_peek(Scanner *scanner);

/*
 * These functions check if the next token is equal to the passed
 * expected token. If it is, they advance the scanner past that token
 * and return 0. Otherwise, they return an error and set the scanner's
 * error.
 */
long scanner_expect_interface_name(Scanner *scanner, char **namep);
long scanner_expect_field_name(Scanner *scanner, char **namep);
long scanner_expect_string(Scanner *scanner, char **stringp);
long scanner_expect_member_name(Scanner *scanner, char **namep);
long scanner_expect_operator(Scanner *scanner, const char *op);
long scanner_expect_type_name(Scanner *scanner, char **namep);

/*
 * The functions read the next token and return true, if the next token
 * is of the expected type. Otherwise, they return false.
 */
bool scanner_read_keyword(Scanner *scanner, const char *keyword);
bool scanner_read_number(Scanner *scanner, ScannerNumber *numberp);
