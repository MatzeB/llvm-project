# facebook T55702441
# REQUIRES: x86
# Test --trace-all-symbols

# RUN: llvm-mc -filetype=obj -triple=x86_64-unknown-linux %s -o %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-unknown-linux \
# RUN: %p/Inputs/trace-symbols-foo-weak.s -o %t1
# RUN: llvm-mc -filetype=obj -triple=x86_64-unknown-linux \
# RUN: %p/Inputs/trace-symbols-foo-strong.s -o %t2
# RUN: ld.lld -shared %t1 -o %t1.so
# RUN: ld.lld -shared %t2 -o %t2.so
# RUN: rm -f %t1.a
# RUN: llvm-ar rcs %t1.a %t1
# RUN: rm -f %t2.a
# RUN: llvm-ar rcs %t2.a %t2

# RUN: ld.lld --trace-all-symbols %t %t1 %t2 -o %t3 | FileCheck %s
# CHECK-DAG: trace-all-symbols.s.tmp: definition of _start
# CHECK-DAG: trace-all-symbols.s.tmp: reference to foo
# CHECK-DAG: trace-all-symbols.s.tmp1: reference to bar
# CHECK-DAG: trace-all-symbols.s.tmp1: common definition of common
# CHECK-DAG: trace-all-symbols.s.tmp1: definition of foo
# CHECK-DAG: trace-all-symbols.s.tmp1: definition of func1
# CHECK-DAG: trace-all-symbols.s.tmp1: reference to func2
# CHECK-DAG: trace-all-symbols.s.tmp2: definition of bar
# CHECK-DAG: trace-all-symbols.s.tmp2: definition of foo
# CHECK-DAG: trace-all-symbols.s.tmp2: definition of func2
# CHECK-DAG: trace-all-symbols.s.tmp1: definition of common

.hidden hsymbol
.globl	_start
.type	_start, @function
_start:
call foo
