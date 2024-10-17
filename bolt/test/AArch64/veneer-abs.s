## Check that llvm-bolt correctly recognizes long absolute thunks.

# RUN: llvm-mc -filetype=obj -triple aarch64-unknown-unknown %s -o %t.o
# RUN: %clang %cflags -fno-PIC -no-pie %t.o -o %t.exe -nostdlib \
# RUN:    -fuse-ld=lld -Wl,-q
# RUN: llvm-objdump -d --disassemble-symbols=__AArch64AbsLongThunk_foo %t.exe \
# RUN:   | FileCheck --check-prefix=CHECK-VENEER %s
# RUN: llvm-objcopy --remove-section .rela.mytext %t.exe
# RUN: llvm-bolt %t.exe -o %t.bolt --elim-link-veneers=true --lite=0
# RUN: llvm-objdump -d -j .text  %t.bolt | FileCheck --check-prefix=CHECK-OUTPUT %s
# RUN: llvm-objdump -d --disassemble-symbols='_start' %t.bolt | FileCheck %s

.text
.balign 4
.global foo
.type foo, %function
foo:
  adrp x1, foo
  ret
.size foo, .-foo

.section ".mytext", "ax"
.balign 4

# CHECK-OUTPUT-NOT: {{.*}} __AArch64AbsLongThunk_foo

.global __AArch64AbsLongThunk_foo
.type __AArch64AbsLongThunk_foo, %function
__AArch64AbsLongThunk_foo:
  ldr x16, .L1
  br x16
# CHECK-VENEER: ldr
# CHECK-VENEER-NEXT: br
.L1:
  .quad foo
.size __AArch64AbsLongThunk_foo, .-__AArch64AbsLongThunk_foo

.global _start
.type _start, %function
_start:
# CHECK: {{.*}} bl {{.*}} <foo>
  bl __AArch64AbsLongThunk_foo
  ret
.size _start, .-_start
