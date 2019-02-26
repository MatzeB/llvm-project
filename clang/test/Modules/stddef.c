// RUN: rm -rf %t
// facebook begin T24847594
// RUN: %clang_cc1 -fmodules -fmodule-map-file=%S/Inputs/StdDef/module.map -fmodules-cache-path=%t -I%S/Inputs/StdDef %s -verify -fno-modules-error-recovery
// facebook end

#include "ptrdiff_t.h"

ptrdiff_t pdt;

size_t st; // expected-error {{missing '#include "include_again.h"'; 'size_t' must be declared before it is used}}
// expected-note@stddef.h:* {{here}}

#include "include_again.h"

size_t st2;
