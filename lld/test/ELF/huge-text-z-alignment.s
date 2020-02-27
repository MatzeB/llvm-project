# facebook T62621959

# RUN: echo "_foo1" > %t.sym_order

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld --enable-huge-text --symbol-ordering-file=%t.sym_order %t.o -o %t2.o
# RUN: llvm-readelf -e %t2.o| FileCheck %s --check-prefix=BEFORE

# BEFORE: Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
# BEFORE-NEXT: PHDR           0x000040 0x0000000000200040 0x0000000000200040 0x0000e0 0x0000e0 R   0x8
# BEFORE-NEXT: LOAD           0x000000 0x0000000000200000 0x0000000000200000 0x000120 0x000120 R   0x1000
# BEFORE-NEXT: LOAD           0x200000 0x0000000000400000 0x0000000000400000 0x000004 0x000004 R E 0x200000
# BEFORE-NEXT: GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x0

# RUN: ld.lld --enable-huge-text --symbol-ordering-file=%t.sym_order -z huge-text-alignment=8192 %t.o -o %t3.o
# RUN: llvm-readelf -e %t3.o| FileCheck %s --check-prefix=AFTER

# AFTER: Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
# AFTER-NEXT: PHDR           0x000040 0x0000000000200040 0x0000000000200040 0x0000e0 0x0000e0 R   0x8
# AFTER-NEXT: LOAD           0x000000 0x0000000000200000 0x0000000000200000 0x000120 0x000120 R   0x1000
# AFTER-NEXT: LOAD           0x002000 0x0000000000202000 0x0000000000202000 0x000004 0x000004 R E 0x2000
# AFTER-NEXT: GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x0

## RUN: ld.lld -z huge-text-alignment=8192 --no-call-graph-profile-sort %t.o -o %t2.o 2>&1 | FileCheck %s --check-prefix=WARN
# WARN: warning: -z huge-text-alignment set, but --enable-huge-text is disabled

# RUN: not ld.lld -z huge-text-alignment=8191 %t.o -o %t2.o 2>&1 | FileCheck %s --check-prefix=ERROR
# ERROR: error: huge-text-alignment: value isn't a power of 2

.section .text.foo1,"ax",@progbits,unique,1
_foo1:
 .byte 0x11
