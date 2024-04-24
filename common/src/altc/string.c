#include "altc/string.h"

size_t alt_strlen(const char *s)
{
	const char *sc;
	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

ssize_t strscpy(char *dest, const char *src, size_t count)
{
	long res = 0;

	if (count == 0)
		return -1; // Too small

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (res)
		dest[res - 1] = '\0';

	return -1; // Too big
}

ssize_t strlcat(char *dest, const char *src, size_t count)
{
	size_t dsize = alt_strlen(dest);
	size_t len = alt_strlen(src);
	size_t res = dsize + len;

	if (res >= count)
		return -1; // Too big (equals because we need null terminator)

	dest += dsize;
	count -= dsize;
	if (len >= count)
		len = count - 1;
	memcpy(dest, src, len);
	dest[len] = 0;
	return res;
}

int alt_strcmp(const char *s1, const char *s2)
{
	while (*s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (*(const unsigned char *)s1 - *(const unsigned char *)(s2 - 1));
}

static int alt_strncmp(const char *s1, const char *s2, size_t n)
{
	unsigned char c1 = '\0';
	unsigned char c2 = '\0';

	if (n >= 4) {
		size_t n4 = n >> 2;
		do {
			c1 = (unsigned char)*s1++;
			c2 = (unsigned char)*s2++;
			if (c1 == '\0' || c1 != c2)
				return c1 - c2;
			c1 = (unsigned char)*s1++;
			c2 = (unsigned char)*s2++;
			if (c1 == '\0' || c1 != c2)
				return c1 - c2;
			c1 = (unsigned char)*s1++;
			c2 = (unsigned char)*s2++;
			if (c1 == '\0' || c1 != c2)
				return c1 - c2;
			c1 = (unsigned char)*s1++;
			c2 = (unsigned char)*s2++;
			if (c1 == '\0' || c1 != c2)
				return c1 - c2;
		} while (--n4 > 0);
		n &= 3;
	}

	while (n > 0) {
		c1 = (unsigned char)*s1++;
		c2 = (unsigned char)*s2++;
		if (c1 == '\0' || c1 != c2)
			return c1 - c2;
		n--;
	}

	return c1 - c2;
}

char *alt_strstr(const char *str, const char *substr)
{
	char c;
	size_t len;

	c = *substr++;
	if (!c)
		return (char *)str; // Trivial empty string case

	len = alt_strlen(substr);
	do {
		char sc;

		do {
			sc = *str++;
			if (!sc)
				return (char *)0;
		} while (sc != c);
	} while (alt_strncmp(str, substr, len) != 0);

	return (char *)(str - 1);
}
