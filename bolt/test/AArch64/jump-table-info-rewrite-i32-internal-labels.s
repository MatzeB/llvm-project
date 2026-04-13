## Check AArch64 .llvm_jump_table_info I32 rewriting when the metadata base and
## some jump-table targets are internal function labels, not emitted BB-entry
## symbols.
##
## This matches threaded-interpreter style dispatch, where BOLT must recover
## output offsets from arbitrary function-local labels to rewrite the table.

# REQUIRES: system-linux

# RUN: %clang %cflags -fuse-ld=lld %s -o %t.exe -Wl,-q
# RUN: llvm-bolt %t.exe -o %t.bolt --jump-tables=move 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LOG
# RUN: llvm-readelf -x .rodata.cold %t.bolt | FileCheck %s --check-prefix=RODATA

# LOG: BOLT-INFO: parsed 1 entries from .llvm_jump_table_info section

# RODATA: Hex dump of section '.rodata.cold':
# RODATA-NEXT: 0x{{[0-9a-f]+}} 10000000 14000000 18000000

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
.Ljtbase:
.Ltmp_adr:
  adr  x10, .Ljtbase
.Ltmp_load:
  ldrsw x11, [x9, w8, uxtw #2]
.Ltmp_add:
  add  x10, x10, x11
.Ltmp_br:
  br   x10

.Lcase0:
  mov w0, #10
.Lcase1:
  add w0, w0, #1
.Lcase2:
  add w0, w0, #1
  ret

.Ldefault:
  mov w0, #99
  ret
  .size _start, .-_start

  .section .rodata,"a",@progbits
  .p2align 2
.LJTI0_0:
  .word (.Lcase0 - .Ljtbase)
  .word (.Lcase1 - .Ljtbase)
  .word (.Lcase2 - .Ljtbase)

  .section .llvm_jump_table_info,"o",@0x6fff4c0e,_start
  .byte 4                               // format 4: 4-byte signed
  .uleb128 66                           // payload bytes after format/length
  .xword .LJTI0_0
  .xword .Ljtbase                       // Base
  .xword .Ltmp_adr                      // Adr instruction
  .xword .Ltmp_load                     // Load instruction
  .xword .Ltmp_add                      // Add instruction
  .xword .Ltmp_br                       // Branch instruction
  .byte 3                               // Number of entries
  .byte 2                               // Number of references
  .xword .Ltmp_ref_adrp                 // Reference
  .xword .Ltmp_ref_add                  // Reference

  .reloc 0, R_AARCH64_NONE
