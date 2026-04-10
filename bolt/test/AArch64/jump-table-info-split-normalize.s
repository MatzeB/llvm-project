## Check that an AArch64 relative jump table described in
## .llvm_jump_table_info is legalized to NORMAL when function splitting moves
## jump targets into a different output section than the base symbol.

# REQUIRES: system-linux

# RUN: llvm-mc -filetype=obj -triple aarch64-unknown-unknown %s -o %t.o
# RUN: link_fdata --no-lbr %s %t.o %t.fdata
# RUN: %clang %cflags %t.o -o %t.exe -Wl,-q -static
# RUN: llvm-bolt %t.exe -o %t.bolt --data %t.fdata --split-functions \
# RUN:   --split-all-cold=1 --jump-tables=move -v=1 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LOG
# RUN: llvm-objdump -d --no-show-raw-insn \
# RUN:   --disassemble-symbols=_start,.dispatch,_start.cold.0 %t.bolt \
# RUN:   | FileCheck %s --check-prefix=DIS

# LOG: BOLT-INFO: updated AArch64 jump table {{.*}} to NORMAL
# LOG: BOLT-INFO: promoted 1 AArch64 jump table(s)

# DIS: <_start>:
# DIS: b.hi

# DIS: <.dispatch>:
# DIS: ldr	x11, [x9, w8, uxtw #3]
# DIS-NEXT: add	x10, x11, xzr
# DIS-NEXT: br	x10

# DIS: <_start.cold.0>:
# DIS: mov	w0, #0xa
# DIS: mov	w0, #0x63

  .text
  .globl _start
  .type _start,%function
_start:
.entry:
# FDATA: 1 _start #.entry# 100
  mov w8, w0
  cmp w0, #2
  b.hi .Ldefault

.dispatch:
# FDATA: 1 _start #.dispatch# 100
.Ltmp_ref_adrp:
  adrp x9, .LJTI0_0
.Ltmp_ref_add:
  add  x9, x9, :lo12:.LJTI0_0
.Ltmp_adr:
  adr  x10, _start
.Ltmp_load:
  ldrb w11, [x9, w8, uxtw]
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
  .space 0x100
.LJTI0_0:
  .byte ((.Lcase0 - _start) >> 2)
  .byte ((.Lcase1 - _start) >> 2)
  .byte ((.Lcase2 - _start) >> 2)

  .section .llvm_jump_table_info,"o",@0x6fff4c0e,_start
  .byte 2                               // format 2: 1-byte unsigned, shr 2
  .uleb128 66                           // payload bytes after format/length
  .xword .LJTI0_0
  .xword _start                         // Base
  .xword .Ltmp_adr                      // Adr instruction
  .xword .Ltmp_load                     // Load instruction
  .xword .Ltmp_add                      // Add instruction
  .xword .Ltmp_br                       // Branch instruction
  .byte 3                               // Number of entries
  .byte 2                               // Number of references
  .xword .Ltmp_ref_adrp                 // Reference
  .xword .Ltmp_ref_add                  // Reference

  .reloc 0, R_AARCH64_NONE
