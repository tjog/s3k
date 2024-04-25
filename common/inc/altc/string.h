#pragma once

#include <stddef.h>
#include <stdint.h>

// The RISC-V GNU toolchain does not seem to define ssize_t
typedef long ssize_t;
_Static_assert(sizeof(ssize_t) == sizeof(size_t));

/**
 * strlcat - Append a length-limited, C-string to another
 * The strlcat() function appends the NUL-terminated
 * string src to the end of dst. It will append at most
 * size - strlen(dst) - 1 bytes, NUL-terminating the result.
 * 
 * This is not the "pure" version, as an error is returned
 * if the src string would not fit
 */
ssize_t strlcat(char *dest, const char *src, size_t count);
/**
 * strlen - Find the length of a string
  */
size_t alt_strlen(const char *s);
/**
 * strlen_s - Find the length of a string, the function returns
 * zero if str is a null pointer and returns strsz if the null
 * character was not found in the first strsz bytes of str.
 */
size_t alt_strnlen_s( const char *s, size_t sz );;
/**
 * strscpy - Copy a C-string into a sized buffer
 */
ssize_t strscpy(char *dest, const char *src, size_t count);
/**
 * strcmp - Compare C-strings, return 0 if equal, negative if
 * s1 precedes s2 lexicographically, and positive if s1 follows s2.
 * The value is the difference between the values when they differed.
*/
int alt_strcmp(const char *s1, const char *s2);
/**
 * strstr - Finds the first occurrence of the null-terminated byte
 * string pointed to by substr in the null-terminated byte string
 * pointed to by str. The terminating null characters are not compared.
 * Returns a pointer to the first character of the found substring in str,
 * or a null pointer if such substring is not found. If substr points to
 * an empty string, str is returned.
*/
char *alt_strstr(const char *str, const char *substr);

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *, int c, size_t n);
