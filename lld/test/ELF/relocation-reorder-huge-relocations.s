# facebook T71528069
# REQUIRES: x86
# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o
# RUN: echo func_explicitly_ordered > %t.symbol-ordering-file
# RUN: ld.lld %t.o --reorder-sections-by-relocation-addend-threshold=0 -o %t.default.out
# RUN: ld.lld %t.o --reorder-sections-by-relocation-addend-threshold=536870912 -o %t.reorder-huge.out
# RUN: ld.lld %t.o --reorder-sections-by-relocation-addend-threshold=536870912 --symbol-ordering-file=%t.symbol-ordering-file -o %t.symbol-ordering.out
# RUN: llvm-objdump -d %t.default.out         | FileCheck %s --check-prefix=DEFAULT
# RUN: llvm-objdump -d %t.reorder-huge.out    | FileCheck %s --check-prefix=JUSTHUGE
# RUN: llvm-objdump -d %t.symbol-ordering.out | FileCheck %s --check-prefix=SYMORDERING

.section .data,"a"
a:
.byte 0

# Test setup:
# - functions with huge PC-relative relocations: huge_relo_negative & huge_relo_positive
# - func_explicitly_ordered: a function that in the second test is explicty ordered using --symbol-order-file
# - func_1, funct_2, func_3, func_4 wrapping around them
# - each of the functions is in its own section and can be reordered

.section	.text.func_1,"ax",@progbits
func_1:
    nop

# The huge_relo_negative function has a huge negative relocation.
.section	.text.huge_relo_negative,"ax",@progbits
huge_relo_negative:
    leaq -1073741824 + a(%rip), %rax

.section	.text.func_2,"ax",@progbits
func_2:
    nop

.section	.text.func_explicitly_ordered,"ax",@progbits
func_explicitly_ordered:
    nop

.section	.text.func_3,"ax",@progbits
func_3:
    nop

# The huge_relo_positive function has a huge positive relocation.
.section	.text.huge_relo_positive,"ax",@progbits
huge_relo_positive:
    leaq 1073741824 + a(%rip), %rax


.section	.text.func_4,"ax",@progbits
func_4:
    nop


# Test #1: by default don't reoder
# DEFAULT:      Disassembly of section .text:
# DEFAULT-EMPTY:
# DEFAULT: <func_1>:
# DEFAULT-NEXT:    nop
# DEFAULT: <huge_relo_negative>:
# DEFAULT-NEXT:    leaq -1073737717(%rip), %rax # 0xffffffffc020216b <a+0xffffffffc0000000>
# DEFAULT: <func_2>:
# DEFAULT-NEXT:    nop
# DEFAULT: <func_explicitly_ordered>:
# DEFAULT-NEXT:    nop
# DEFAULT: <func_3>:
# DEFAULT-NEXT:    nop
# DEFAULT: <huge_relo_positive>:
# DEFAULT-NEXT:    leaq 1073745921(%rip), %rax # 0x4020216b <a+0x40000000>
# DEFAULT: <func_4>:
# DEFAULT-NEXT:    nop


# Test #2: if --reorder-sections-by-relocation-addend-threshold passed reorder huge_* sections
# - the function with a huge negative relocation gets moved to the top (first match)
# - the func_1, func_2, func_3, func_4 functions are kept in the input order
# - func_explicitly_ordered is not ordered explicty in this test.
# - the function with a huge negative relocation gets moved to the bottom (last match)

# JUSTHUGE:      Disassembly of section .text:
# JUSTHUGE-EMPTY:
# JUSTHUGE: <huge_relo_negative>:
# JUSTHUGE-NEXT:    leaq -1073737715(%rip), %rax # 0xffffffffc020216c <a+0xffffffffc0000000>
# JUSTHUGE: <func_1>:
# JUSTHUGE-NEXT:    nop
# JUSTHUGE: <func_2>:
# JUSTHUGE-NEXT:    nop
# JUSTHUGE: <func_explicitly_ordered>:
# JUSTHUGE-NEXT:    nop
# JUSTHUGE: <func_3>:
# JUSTHUGE-NEXT:    nop
# JUSTHUGE: <func_4>:
# JUSTHUGE-NEXT:    nop
# JUSTHUGE: <huge_relo_positive>:
# JUSTHUGE-NEXT:    leaq 1073745920(%rip), %rax # 0x4020216c <a+0x40000000>


# Test #3: --symbol-ordering-file takes precendece over --reorder-sections-by-relocation-addend-threshold
# - test relative function order similar to test #2
# - but make sure the symbol from --symbol-ordering-file takes precendece:
#   func_explicitly_ordered will be placed first.
# SYMORDERING:      Disassembly of section .text:
# SYMORDERING-EMPTY:
# SYMORDERING: <func_explicitly_ordered>:
# SYMORDERING-NEXT:    nop
# SYMORDERING: <huge_relo_negative>:
# SYMORDERING-NEXT:    leaq -1073737717(%rip), %rax # 0xffffffffc020216b <a+0xffffffffc0000000>
# SYMORDERING: <func_1>:
# SYMORDERING-NEXT:    nop
# SYMORDERING: <func_2>:
# SYMORDERING-NEXT:    nop
# SYMORDERING: <func_3>:
# SYMORDERING-NEXT:    nop
# SYMORDERING: <func_4>:
# SYMORDERING-NEXT:    nop
# SYMORDERING: <huge_relo_positive>:
# SYMORDERING-NEXT:    leaq 1073745920(%rip), %rax # 0x4020216b <a+0x40000000>
