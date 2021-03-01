// SPDX-License-Identifier: Apache-2.0
// Copied from https://github.com/c-util/c-utf8
// Authors:
//      David Rheinsberg <david.rheinsberg@gmail.com>
//      Tom Gundersen <teg@jklm.no>

/*
 * UTF-8 Implementation
 *
 * For highlevel documentation of the API see the header file and the docbook
 * comments. This implementation is inspired in part by Rust's std::str.
 *
 * So far only validation helpers are implemented, as those seem the most
 * critical.
 */

#include <stddef.h>
#include <stdint.h>
#include "util.h"
#include "c-utf8.h"

#define c_align_to(n, a) (((n) + (a) - 1) & (~((a) - 1)))
#define _c_unlikely_(_x) (__builtin_expect(!!(_x), 0))

/* The following constants are truncated on 32-bit machines */
#define C_UTF8_ASCII_MASK ((size_t)UINT64_C(0x8080808080808080))
#define C_UTF8_ASCII_SUB ((size_t)UINT64_C(0x0101010101010101))

static inline int c_utf8_word_is_ascii(const size_t *word) {
        /* True unless any byte is NULL or has the MSB set. */
        return ((((*word - C_UTF8_ASCII_SUB) | *word) & C_UTF8_ASCII_MASK) == 0);
}

/**
 * c_utf8_verify_ascii() - verify that a string is ASCII encoded
 * @strp:               pointer to string to verify
 * @lenp:               pointer to length of string
 *
 * Up to the first @lenp bytes of the string pointed to by @strp is
 * verified to be ASCII encoded, and @strp and @lenp are updated to
 * point to the first non-ASCII character or the first NULL of the
 * string, and the remaining number of bytes of the string,
 * respectively.
 *
 * If @lenp is NULL the string is scanned until the first invalid
 * byte, without any upper bound on its length.
 */
static void c_utf8_verify_ascii(const char **strp, size_t *lenp) {
        unsigned char *str = (unsigned char *)*strp;
        size_t len = lenp ? *lenp : (size_t)-1;

        while (len > 0 && *str < 128) {
                if ((void *)c_align_to((unsigned long)str, sizeof(size_t)) == str) {
                        /*
                         * If the string is aligned to a word boundary, scan two
                         * words at a time for any NULL or non-ASCII characters.
                         *
                         * We do two words at a time to take advantage of the
                         * compiler being able to use SIMD instructions where
                         * available.
                         */
                        while (len >= 2 * sizeof(size_t)) {
                                if (!c_utf8_word_is_ascii((size_t *)str) ||
                                    !c_utf8_word_is_ascii(((size_t *)str) + 1))
                                        break;

                                str += 2 * sizeof(size_t);
                                len -= 2 * sizeof(size_t);
                        }


                        /*
                         * Find the actual end of the ASCII-portion of the string.
                         */
                        while (len > 0 && *str < 128) {
                                if (_c_unlikely_(*str == 0x00))
                                        goto out;
                                ++str;
                                --len;
                        }
                } else {
                        /*
                         * The string was not aligned, scan one character at a time until
                         * it is.
                         */
                        if (_c_unlikely_(*str == 0x00))
                                goto out;
                        ++str;
                        --len;
                }
        }

out:
        *strp = (char *)str;
        if (lenp)
                *lenp = len;
}

#define C_UTF8_CHAR_IS_TAIL(_x)         (((_x) & 0xC0) == 0x80)

/**
 * c_utf8_verify() - verify that a string is UTF-8 encoded
 * @strp:               pointer to string to verify
 * @lenp:               pointer to length of string, or NULL
 *
 * Up to the first @lenp bytes of the string pointed to by @strp is
 * verified to be UTF-8 encoded, and @strp and @lenp are updated to
 * point to the first non-UTF-8 character or the first NULL of the
 * string, and the remaining number of bytes of the string,
 * respectively.
 *
 * If @lenp is NULL the string is scanned until the first invalid
 * byte, without any upper bound on its length.
 */
void c_utf8_verify(const char **strp, size_t *lenp) {
        unsigned char *str = (unsigned char *)*strp;
        size_t len = lenp ? *lenp : (size_t)-1;

        /* See Unicode 10.0.0, Chapter 3, Section D92 */

        while (len > 0) {
                switch (*str) {
                        case 0x00:
                                goto out;
                        case 0x01 ... 0x7F:
                                /*
                                 * Special-case and optimize the ASCII case.
                                 */
                                c_utf8_verify_ascii((const char **)&str, &len);

                                break;
                        case 0xC2 ... 0xDF:
                                if (_c_unlikely_(len < 2))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 1))))
                                        goto out;

                                str += 2;
                                len -= 2;

                                break;
                        case 0xE0:
                                if (_c_unlikely_(len < 3))
                                        goto out;
                                if (_c_unlikely_(*(str + 1) < 0xA0 || *(str + 1) > 0xBF))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;

                                str += 3;
                                len -= 3;

                                break;
                        case 0xE1 ... 0xEC:
                                if (_c_unlikely_(len < 3))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 1))))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;

                                str += 3;
                                len -= 3;

                                break;
                        case 0xED:
                                if (_c_unlikely_(len < 3))
                                        goto out;
                                if (_c_unlikely_(*(str + 1) < 0x80 || *(str + 1) > 0x9F))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;

                                str += 3;
                                len -= 3;

                                break;
                        case 0xEE ... 0xEF:
                                if (_c_unlikely_(len < 3))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 1))))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;

                                str += 3;
                                len -= 3;

                                break;
                        case 0xF0:
                                if (_c_unlikely_(len < 4))
                                        goto out;
                                if (_c_unlikely_(*(str + 1) < 0x90 || *(str + 1) > 0xBF))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 3))))
                                        goto out;

                                str += 4;
                                len -= 4;

                                break;
                        case 0xF1 ... 0xF3:
                                if (_c_unlikely_(len < 4))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 1))))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 3))))
                                        goto out;

                                str += 4;
                                len -= 4;

                                break;
                        case 0xF4:
                                if (_c_unlikely_(len < 4))
                                        goto out;
                                if (_c_unlikely_(*(str + 1) < 0x80 || *(str + 1) > 0x8F))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 2))))
                                        goto out;
                                if (_c_unlikely_(!C_UTF8_CHAR_IS_TAIL(*(str + 3))))
                                        goto out;

                                str += 4;
                                len -= 4;

                                break;
                        default:
                                goto out;
                }
        }

out:
        *strp = (char *)str;
        if (lenp)
                *lenp = len;
}
