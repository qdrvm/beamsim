#pragma once

#include <cstdio>
#include <cstdlib>

// "assert" doesn't work
#define assert2(c)                                       \
  if (not(c)) {                                          \
    printf("assert2(%s) %s:%d", #c, __FILE__, __LINE__); \
    abort();                                             \
  }
