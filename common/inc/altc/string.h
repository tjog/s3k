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
size_t strlen(const char *s);
/**
 * strscpy - Copy a C-string into a sized buffer
 */
ssize_t strscpy(char *dest, const char *src, size_t count);
/**
 * strcmp - Compare C-strings, return 0 if equal, negative if
 * s1 precedes s2 lexicographically, and positive if s1 follows s2.
 * The value is the difference between the values when they differed.
*/
int strcmp(const char *s1, const char *s2);

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memset(void *, int c, size_t n);
