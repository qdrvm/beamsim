#pragma once

#if defined(__cplusplus)
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

EXTERN_C void OPENSSL_free(void *ptr);

EXTERN_C void CRYPTO_library_init();
