// SPDX-License-Identifier: Apache-2.0
// Copied from https://github.com/c-util/c-utf8
// Authors:
//      David Rheinsberg <david.rheinsberg@gmail.com>
//      Tom Gundersen <teg@jklm.no>
#pragma once

/*
 * UTF-8 Handling in Standard ISO-C11
 *
 * This library provides an UTF-8 API, that is fully implemented in ISO-C11
 * and has no external dependencies.
 *
 * UTF-8 is defined in Unicode 10.0.0, Chapter 3, Section D92. We deviate from
 * the specification only by considering U+0000 an invalid codepoint, as its
 * encoding in UTF-8 would be the C end-of-string character.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

void c_utf8_verify(const char **strp, size_t *lenp);

#ifdef __cplusplus
}
#endif
