## Check that AArch64 jump-table metadata is recognized from suffixed
## .llvm_jump_table_info sections during instrumentation.
##
## The base for the table is an internal label. If the suffixed metadata
## section is ignored, BOLT treats that address as escaped and fails to
## post-process the indirect branch.

# REQUIRES: system-linux,bolt-runtime,target=aarch64{{.*}}

# RUN: %clang %cflags -fuse-ld=lld %s -o %t.exe -Wl,-q
# RUN: llvm-bolt %t.exe -o %t.bolt --instrument --instrumentation-file=%t.fdata \
# RUN:   --instrumentation-sleep-time=1 -v=1 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LOG

# LOG: BOLT-INFO: parsed 1 entries from .llvm_jump_table_info section
# LOG: BOLT-INFO: forcing -jump-tables=move for instrumentation
# LOG-NOT: potentially escaped address
# LOG-NOT: failed to post-process indirect branches for _start

  .text
  .globl _start
  .type _start,%function
_start:
  cmp w0, #2
  b.hi .Ldefault
  mov w8, w0

.Ltmp_ref_adr:
  adr  x9, .LJTI0_0
.Ltmp_adr:
  adr  x10, .Lcase0
.Ltmp_load:
  ldrb w11, [x9, x8]
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
.LJTI0_0:
  .byte ((.Lcase0 - .Lcase0) >> 2)
  .byte ((.Lcase1 - .Lcase0) >> 2)
  .byte ((.Lcase2 - .Lcase0) >> 2)

  .section .llvm_jump_table_info._start,"o",@0x6fff4c0e,_start
  .byte 2                               // format 2: 1-byte unsigned, shr 2
  .uleb128 58                           // payload bytes after format/length
  .xword .LJTI0_0
  .xword .Lcase0                        // Base
  .xword .Ltmp_adr                      // Adr instruction
  .xword .Ltmp_load                     // Load instruction
  .xword .Ltmp_add                      // Add instruction
  .xword .Ltmp_br                       // Branch instruction
  .byte 3                               // Number of entries
  .byte 1                               // Number of references
  .xword .Ltmp_ref_adr                  // Reference

  // Force relocation mode.
  .reloc 0, R_AARCH64_NONE
