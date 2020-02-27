# facebook T62621959

# REQUIRES: x86

# RUN: echo "_foo3 _foo2 50" > %t.call_graph

# RUN: echo "_foo3  " > %t.sym_order
# RUN: echo "  _foo2" >> %t.sym_order

# RUN: llvm-mc -filetype=obj -triple=x86_64-pc-linux %s -o %t.o

# RUN: ld.lld %t.o -o %t_cg_profile.out
# RUN: llvm-objdump -s %t_cg_profile.out| FileCheck %s --check-prefix=BEFORE_TEXT_CONTENT
# RUN: ld.lld --call-graph-ordering-file %t.call_graph %t.o -o %t_cg_order.out
# RUN: llvm-objdump -s %t_cg_order.out| FileCheck %s --check-prefix=BEFORE_TEXT_CONTENT
# RUN: ld.lld --symbol-ordering-file %t.sym_order %t.o -o %t_sym_order.out
# RUN: llvm-objdump -s %t_sym_order.out| FileCheck %s --check-prefix=BEFORE_TEXT_CONTENT

# BEFORE_TEXT_CONTENT:      Contents of section .text:
# BEFORE_TEXT_CONTENT-NEXT:  3322cccc 11

# RUN: llvm-readelf -e %t_cg_profile.out| FileCheck %s --check-prefix=BEFORE_ALIGNMENT
# RUN: llvm-readelf -e %t_cg_order.out| FileCheck %s --check-prefix=BEFORE_ALIGNMENT
# RUN: llvm-readelf -e %t_sym_order.out| FileCheck %s --check-prefix=BEFORE_ALIGNMENT

# BEFORE_ALIGNMENT: Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
# BEFORE_ALIGNMENT-NEXT: PHDR           0x000040 0x0000000000200040 0x0000000000200040 0x0000e0 0x0000e0 R   0x8
# BEFORE_ALIGNMENT-NEXT: LOAD           0x000000 0x0000000000200000 0x0000000000200000 0x000120 0x000120 R   0x1000
# BEFORE_ALIGNMENT-NEXT: LOAD           0x000120 0x0000000000201120 0x0000000000201120 0x000005 0x000005 R E 0x1000
# BEFORE_ALIGNMENT-NEXT: GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x0

# BEFORE_ALIGNMENT: Section to Segment mapping:
# BEFORE_ALIGNMENT-NEXT: Segment Sections...
# BEFORE_ALIGNMENT: 02     .text

# RUN: llvm-nm -n %t_cg_profile.out| FileCheck %s --check-prefix=BEFORE_SYM
# RUN: llvm-nm -n %t_cg_order.out| FileCheck %s --check-prefix=BEFORE_SYM
# RUN: llvm-nm -n %t_sym_order.out| FileCheck %s --check-prefix=BEFORE_SYM

# BEFORE_SYM: {{[0-9]+}} t _foo3
# BEFORE_SYM-NEXT: {{[0-9]+}} t _foo2
# BEFORE_SYM-NEXT: {{[0-9]+}} t _foo1

# RUN: ld.lld --enable-huge-text %t.o -o %t_cg_profile2.out
# RUN: llvm-objdump -s %t_cg_profile2.out| FileCheck %s --check-prefix=AFTER_TEXT_CONTENT
# RUN: ld.lld --call-graph-ordering-file %t.call_graph --enable-huge-text %t.o -o %t_cg_order2.out
# RUN: llvm-objdump -s %t_cg_order2.out| FileCheck %s --check-prefix=AFTER_TEXT_CONTENT
# RUN: ld.lld --symbol-ordering-file %t.sym_order --enable-huge-text %t.o -o %t_sym_order2.out
# RUN: llvm-objdump -s %t_sym_order2.out| FileCheck %s --check-prefix=AFTER_TEXT_CONTENT

# AFTER_TEXT_CONTENT:      Contents of section .text:
# AFTER_TEXT_CONTENT-NEXT:  3322cccc 11

# RUN: llvm-readelf -e %t_cg_profile2.out| FileCheck %s --check-prefix=AFTER_ALIGNMENT
# RUN: llvm-readelf -e %t_cg_order2.out| FileCheck %s --check-prefix=AFTER_ALIGNMENT
# RUN: llvm-readelf -e %t_sym_order2.out| FileCheck %s --check-prefix=AFTER_ALIGNMENT

# AFTER_ALIGNMENT: Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
# AFTER_ALIGNMENT-NEXT: PHDR           0x000040 0x0000000000200040 0x0000000000200040 0x0000e0 0x0000e0 R   0x8
# AFTER_ALIGNMENT-NEXT: LOAD           0x000000 0x0000000000200000 0x0000000000200000 0x000120 0x000120 R   0x1000
# AFTER_ALIGNMENT-NEXT: LOAD           0x200000 0x0000000000400000 0x0000000000400000 0x000005 0x000005 R E 0x200000
# AFTER_ALIGNMENT-NEXT: GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x0

# AFTER_ALIGNMENT: Section to Segment mapping:
# AFTER_ALIGNMENT-NEXT: Segment Sections...
# AFTER_ALIGNMENT: 02     .text

# RUN: llvm-nm -n %t_cg_profile2.out| FileCheck %s --check-prefix=AFTER_SYM
# RUN: llvm-nm -n %t_cg_order2.out| FileCheck %s --check-prefix=AFTER_SYM
# RUN: llvm-nm -n %t_sym_order2.out| FileCheck %s --check-prefix=AFTER_SYM

# AFTER_SYM: {{[0-9]+}} T __hot_start
# AFTER_SYM-NEXT: {{[0-9]+}} t _foo3
# AFTER_SYM-NEXT: {{[0-9]+}} t _foo2
# AFTER_SYM-NEXT: {{[0-9]+}} T __hot_end
# AFTER_SYM-NEXT: {{[0-9]+}} t _foo1

.section .text.foo1,"ax",@progbits,unique,1
_foo1:
 .byte 0x11

.section .text.foo2,"ax",@progbits,unique,2
_foo2:
 .byte 0x22

.section .text.foo3,"ax",@progbits,unique,3
_foo3:
 .byte 0x33

.cg_profile _foo3, _foo2, 50
