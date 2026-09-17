#pragma once
/* Deterministic fault-test digest only; does not test SHA-256 cryptography. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct { uint32_t value; } mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *c) { c->value = 2166136261u; }
static inline void mbedtls_sha256_free(mbedtls_sha256_context *c) { (void)c; }
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *c, int bits) { (void)bits; mbedtls_sha256_init(c); return 0; }
static inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const unsigned char *p, size_t n) { while (n--) c->value = (c->value ^ *p++) * 16777619u; return 0; }
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, unsigned char *out) { for (int i = 0; i < 8; i++) memcpy(out + i * 4, &c->value, 4); return 0; }
static inline int mbedtls_sha256(const unsigned char *p, size_t n, unsigned char *out, int bits) { mbedtls_sha256_context c; mbedtls_sha256_starts(&c, bits); mbedtls_sha256_update(&c, p, n); return mbedtls_sha256_finish(&c, out); }
