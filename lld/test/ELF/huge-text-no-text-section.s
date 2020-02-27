# facebook T62621959

# REQUIRES: x86
# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: ld.lld --enable-huge-text %t.o -o %t.out

# CHECK: warning: .text section not found, cannot hugify .text

.section .foo
 .byte 0x11
