#pragma once

#include "parse-error.h"

typedef struct Scanner Scanner;

typedef struct {
        bool is_double;
        union {
                double d;
                int64_t i;
        };
} ScannerNumber;

long scanner_new_varlink(Scanner **scannerp, const char *string);
long scanner_new_json(Scanner **scannerp, const char *string);
void scanner_free(Scanner *scanner);
void scanner_freep(Scanner **scannerp);

/*
 * Sets the error on scanner and returns false.
 */
bool scanner_error(Scanner *scanner, const char *fmt, ...) __attribute__((format (printf, 2, 3)));

/*
 * If errorp is not NULL and scanner has an error, copies the error into
 * errorp.
 */
void scanner_steal_error(Scanner *scanner, VarlinkParseError **errorp);

/*
 * If the scanner was created with scanner_new_varlink(), return the
 * last docstring that the scanner encountered (a multi-line comment
 * that starts at the beginning of a line and immediately preceded a
 * token). Returns NULL if the scanner wasn't advanced or no docstring
 * was found since the last call to this function.
 */
char *scanner_get_last_docstring(Scanner *scanner);

/*
 * Advances the scanner and returns the first character of the next
 * token.
 */
char scanner_peek(Scanner *scanner);

/*
 * These functions check if the next token is equal to the passed
 * expected token. If it is, they advance the scanner past that token
 * and return true. Otherwise, they set the scanner's error and return
 * false.
 */
bool scanner_expect_char(Scanner *scanner, char c);
bool scanner_expect_keyword(Scanner *scanner, const char *keyword);

/*
 * The functions read the next token and return true, if the next token
 * is of the expected type. Otherwise, they set the scanner's error and
 * return false.
 */
bool scanner_read_identifier(Scanner *scanner, bool (*allow)(char, bool), char **identifierp);
bool scanner_read_string(Scanner *scanner, char **stringp);
bool scanner_read_number(Scanner *scanner, ScannerNumber *numberp);
bool scanner_read_uint(Scanner *scanner, uint64_t *uintp);
bool scanner_read_arrow(Scanner *scanner);
