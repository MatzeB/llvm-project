// facebook T59242408 D18533994
// RUN: rm -rf %t
// expected-no-diagnostics

// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t -Rmodule-build -fno-modules-error-recovery \
// RUN:            -fmodule-name=a -emit-module %S/Inputs/explicit-build/module.modulemap -o %t/a.pcm

// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t -Rmodule-build -fno-modules-error-recovery \
// RUN:            -fmodule-file=a=%t/a.pcm \
// RUN:            -fmodule-name=b -emit-module %S/Inputs/explicit-build/module.modulemap -o %t/b.pcm

// RUN: %clang_cc1 -x c++ -std=c++11 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t -Rmodule-build -fno-modules-error-recovery \
// RUN:            -fmodule-file=a=%t/a.pcm \
// RUN:            -fmodule-file=b=%t/b.pcm \
// RUN:            -fmodule-name=c -emit-module %S/Inputs/explicit-build/module.modulemap -o %t/c.pcm

// RUN: mv %t/a.pcm %t/a.moved.pcm
// RUN: mv %t/b.pcm %t/b.moved.pcm
// RUN: mv %t/c.pcm %t/c.moved.pcm
//
// RUN: %clang_cc1 -x pcm -std=c++11 -fmodules -fimplicit-module-maps -fmodules-cache-path=%t -Rmodule-build -fno-modules-error-recovery \
// RUN:            -fmodule-file=a=%t/a.moved.pcm \
// RUN:            -fmodule-file=b=%t/b.moved.pcm \
// RUN:            %t/c.moved.pcm -o %t/c.o
// expected-no-diagnostics

#include <stdio.h>
