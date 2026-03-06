## Check AArch64 .llvm_jump_table_info U8_X4 rewriting.
##
## Format 2 encodes entries as 1-byte values, interpreted as:
##   target = base + (entry << 2)
##
## After block reordering, BOLT should rewrite jump table bytes accordingly.

# REQUIRES: system-linux

# RUN: %clang %cflags -fuse-ld=lld %s -o %t.exe -Wl,-q
# RUN: llvm-bolt %t.exe -o %t.bolt --reorder-blocks=reverse --jump-tables=move 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LOG
# RUN: llvm-readelf -x .rodata.cold %t.bolt | FileCheck %s --check-prefix=RODATA

# LOG: BOLT-INFO: parsed 1 entries from .llvm_jump_table_info section
# LOG: BOLT-INFO: basic block reordering modified layout of 1 functions

# RODATA: Hex dump of section '.rodata.cold':
# RODATA-NEXT: 0x{{[0-9a-f]+}} 0c0905

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
  add w0, w0, #1
  ret

.Lcase2:
  mov w0, #12
  add w0, w0, #2
  sub w0, w0, #1
  ret

.Ldefault:
  mov w0, #99
  ret
  .size _start, .-_start

  .section .rodata,"a",@progbits
  .space 0x200000
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

  // Force relocation mode.
  .reloc 0, R_AARCH64_NONE
