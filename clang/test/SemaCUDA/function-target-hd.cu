// facebook begin T63033708
// Test the Sema analysis of caller-callee relationships of host device
// functions when compiling CUDA code. There are 4 permutations of this test as
// host and device compilation are separate compilation passes, and clang has
// an option to allow host calls from host device functions. __CUDA_ARCH__ is
// defined when compiling for the device and TEST_WARN_HD when host calls are
// allowed from host device functions. So for example, if __CUDA_ARCH__ is
// defined and TEST_WARN_HD is not then device compilation is happening but
// host device functions are not allowed to call host functions.

// RUN: %clang_cc1 -fsyntax-only -verify %s
// RUN: %clang_cc1 -fsyntax-only -fcuda-is-device -verify %s
// RUN: %clang_cc1 -fsyntax-only -fcuda-allow-host-calls-from-host-device -verify %s -DTEST_WARN_HD -Werror
// RUN: %clang_cc1 -fsyntax-only -fcuda-is-device -fcuda-allow-host-calls-from-host-device -verify %s -DTEST_WARN_HD -Werror

#include "Inputs/cuda.h"

__host__ void hd1h(void);
#if defined(__CUDA_ARCH__) && !defined(TEST_WARN_HD)
// expected-note@-2 {{'hd1h' declared here}}
#endif
__device__ void hd1d(void);
#ifndef __CUDA_ARCH__
// expected-note@-2 {{'hd1d' declared here}}
#endif
__host__ void hd1hg(void);
__device__ void hd1dg(void);
#ifdef __CUDA_ARCH__
__host__ void hd1hig(void);
#if !defined(TEST_WARN_HD)
// expected-note@-2 {{'hd1hig' declared here}}
#endif
#else
__device__ void hd1dig(void); // expected-note {{'hd1dig' declared here}}
#endif
__host__ __device__ void hd1hd(void);
__global__ void hd1g(void);
#if defined(__CUDA_ARCH__)
// expected-note@-2 {{'hd1g' declared here}}
#endif

__host__ __device__ void hd1(void) {
#if defined(TEST_WARN_HD) && defined(__CUDA_ARCH__)
#endif
  hd1d();
#ifndef __CUDA_ARCH__
// expected-error@-2 {{reference to __device__ function 'hd1d' in __host__ __device__ function}}
#endif
  hd1h();
#if defined(__CUDA_ARCH__)
#if !defined(TEST_WARN_HD)
// expected-error@-3 {{reference to __host__ function 'hd1h' in __host__ __device__ function}}
#else
// expected-error@-5 {{calling __host__ function hd1h from __host__ __device__ function hd1 can lead to runtime errors}}
#endif
#endif

  // No errors as guarded
#ifdef __CUDA_ARCH__
  hd1d();
#else
  hd1h();
#endif

  // Errors as incorrectly guarded
#ifndef __CUDA_ARCH__
  hd1dig(); // expected-error {{reference to __device__ function 'hd1dig' in __host__ __device__ function}}
#else
  hd1hig();
#ifndef TEST_WARN_HD
// expected-error@-2 {{reference to __host__ function 'hd1hig' in __host__ __device__ function}}
#else
// expected-error@-4 {{calling __host__ function hd1hig from __host__ __device__ function hd1 can lead to runtime errors}}
#endif

#endif

  hd1hd();
  hd1g<<<1, 1>>>();
#ifdef __CUDA_ARCH__
  // expected-error@-2 {{reference to __global__ function 'hd1g' in __host__ __device__ function}}
#endif
}
// facebook end
