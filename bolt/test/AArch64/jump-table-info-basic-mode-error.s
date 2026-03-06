## Ensure AArch64 jump tables from .llvm_jump_table_info force
## -jump-tables=move when --jump-tables=basic is requested.

# REQUIRES: system-linux

# RUN: %clang %cflags -fuse-ld=lld %s -o %t.exe -Wl,-q
# RUN: llvm-bolt %t.exe -o %t.bolt --jump-tables=basic 2>&1 | FileCheck %s

# CHECK: BOLT-INFO: forcing -jump-tables=move as PIC jump table was detected in function _start

  .text
  .globl _start
  .type _start,%function
_start:
  mov w8, w0
  cmp w0, #2
  b.hi .Ldefault

.Ltmp_ref_adrp:
  adrp x9, .LJTI0_0
.Ltmp_ref_add:
  add  x9, x9, :lo12:.LJTI0_0
.Ltmp_adr:
  adr  x10, _start
.Ltmp_load:
  ldrh w11, [x9, w8, uxtw #1]
.Ltmp_add:
  add  x10, x10, x11, lsl #2
.Ltmp_br:
  br   x10

.Lcase0:
  mov w0, #10
  ret

.Lcase1:
  mov w0, #11
  ret

.Lcase2:
  mov w0, #12
  ret

.Ldefault:
  mov w0, #99
  ret
  .size _start, .-_start

  .section .rodata,"a",@progbits
  .space 0x200000
  .p2align 1
.LJTI0_0:
  .hword ((.Lcase0 - _start) >> 2)
  .hword ((.Lcase1 - _start) >> 2)
  .hword ((.Lcase2 - _start) >> 2)

  .section .llvm_jump_table_info,"o",@0x6fff4c0e,_start
  .byte 3
  .uleb128 66
  .xword .LJTI0_0
  .xword _start
  .xword .Ltmp_adr
  .xword .Ltmp_load
  .xword .Ltmp_add
  .xword .Ltmp_br
  .byte 3
  .byte 2
  .xword .Ltmp_ref_adrp
  .xword .Ltmp_ref_add

  .reloc 0, R_AARCH64_NONE
