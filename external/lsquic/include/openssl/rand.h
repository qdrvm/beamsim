#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

EXTERN_C int RAND_bytes(uint8_t *buf, size_t len);
