#pragma once

#include "stddef.h"
#include "stdint.h"

typedef enum {
	SIGN_SUCCESS,
	SIGN_ERR_FILE_OPEN,
	SIGN_ERR_FILE_READ,
	SIGN_ERR_FILE_WRITE,
	SIGN_ERR_INVALID_CAPABILITY,
	SIGN_ERR_SERVER_OUT_OF_CAPABILITY_SLOTS,
	SIGN_ERR_SERVER_DERIVATION,
	SIGN_ERR_INVALID_OPERATION_CODE,
} sign_err_t;

/* data[0] is the operation code */
typedef enum {
	sign_client_sign_file, /* server returns the digest in data[1] */
	sign_client_request_public_key,
} sign_client_ops;

/*
When a user wants to sign a document or message, they use their private key to generate a
unique digital signature for that specific content. This process usually involves hashing
the content to create a fixed-size digest, and then encrypting the digest with the private
key.
*/

/* Should really be bignums in these structures, but as proof of concept small keys are used */
typedef struct rsa_private_key {
	uint64_t n;
	uint64_t d;
} rsa_private_key_t;

typedef struct rsa_public_key {
	uint64_t n;
	uint64_t e;
} rsa_public_key_t;

long atol(unsigned char *cp)
{
	long number;

	for (number = 0; ('0' <= *cp) && (*cp <= '9'); cp++)
		number = (number * 10) + (*cp - '0');

	return (number);
}

int atoi(unsigned char *cp)
{
	return (int)atol(cp);
}

int alt_printf(const char *fmt, ...);

uint64_t modular_pow_u64(uint64_t base, uint64_t exponent, uint64_t modulus)
{
	if (modulus == 1)
		return 0;
	// Assert :: (modulus - 1) * (modulus - 1) does not overflow base
	uint64_t result = 1;
	base = base % modulus;
	while (exponent > 0) {
		if ((exponent & 1) == 1)
			result = (result * base) % modulus;
		exponent = exponent >> 1;
		base = (base * base) % modulus;
	}
	return result;
}

void rsa_mod_pow(uint64_t exp, uint64_t mod, uint8_t *buf, size_t buf_len)
{
	alt_printf("exp=%d, mod=%d\n", exp, mod);
#define RSA_BITS_TYPE uint16_t
	RSA_BITS_TYPE *b = (RSA_BITS_TYPE *)buf;
	uint8_t *end = buf + buf_len;

	for (; b <= (RSA_BITS_TYPE *)end - sizeof(RSA_BITS_TYPE); b++) {
		*b = (RSA_BITS_TYPE)modular_pow_u64(*b, exp, mod);
	}
}