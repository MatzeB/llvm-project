# facebook T55702441
# REQUIRES: x86
# Test --trace-symbols-from-file=file and --trace-symbol=symbol

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

# RUN: ld.lld --trace-symbols-from-file=%t %t %t1 %t2 -o %t3 | FileCheck --check-prefix=FILE0 %s
# FILE0-DAG: trace-file.s.tmp: definition of _start
# FILE0-DAG: trace-file.s.tmp: reference to foo

# RUN: ld.lld --trace-symbols-from-file=%t1 %t %t1 %t2 -o %t3 | FileCheck --check-prefix=FILE1 %s
# FILE1-DAG: trace-file.s.tmp1: reference to bar
# FILE1-DAG: trace-file.s.tmp1: common definition of common
# FILE1-DAG: trace-file.s.tmp1: definition of foo
# FILE1-DAG: trace-file.s.tmp1: definition of func1
# FILE1-DAG: trace-file.s.tmp1: reference to func2
# FILE1-DAG: trace-file.s.tmp1: definition of common

# RUN: ld.lld --trace-symbols-from-file=%t --trace-symbols-from-file=%t1 %t %t1 %t2 -o %t3 | FileCheck --check-prefix=FILE0AND1 %s
# FILE0AND1-DAG: trace-file.s.tmp: definition of _start
# FILE0AND1-DAG: trace-file.s.tmp: reference to foo
# FILE0AND1-DAG: trace-file.s.tmp1: reference to bar
# FILE0AND1-DAG: trace-file.s.tmp1: common definition of common
# FILE0AND1-DAG: trace-file.s.tmp1: definition of foo
# FILE0AND1-DAG: trace-file.s.tmp1: definition of func1
# FILE0AND1-DAG: trace-file.s.tmp1: reference to func2
# FILE0AND1-DAG: trace-file.s.tmp1: definition of common

# RUN: ld.lld --trace-symbols-from-file=%t --trace-symbol=foo %t %t1 %t2 -o %t3 | FileCheck --check-prefix=FILE0FOO %s
# FILE0FOO-DAG: trace-file.s.tmp: definition of _start
# FILE0FOO-DAG: trace-file.s.tmp: reference to foo
# FILE0FOO-DAG: trace-file.s.tmp1: definition of foo
# FILE0FOO-DAG: trace-file.s.tmp2: definition of foo

.hidden hsymbol
.globl	_start
.type	_start, @function
_start:
call foo
