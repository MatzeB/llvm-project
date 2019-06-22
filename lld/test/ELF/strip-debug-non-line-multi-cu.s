# facebook begin T37438891
# REQUIRES: x86
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t.o
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %p/Inputs/strip-debug-non-line-multi-cu-bar.s -o %tbar.o
# RUN: ld.lld %t.o %tbar.o -o %t --strip-debug-non-line
# RUN: llvm-readobj --sections --symbols %t | FileCheck %s --check-prefix=CHECK-READOBJ
# RUN: llvm-dwarfdump --verify %t
# RUN: llvm-dwarfdump --debug-info %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-INFO
# RUN: llvm-dwarfdump --debug-abbrev %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-ABBREV
# RUN: llvm-dwarfdump --debug-aranges %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-ARANGES

# CHECK-READOBJ: Name: .debug_str
# CHECK-READOBJ: Name: .{{z?}}debug_abbrev
# CHECK-READOBJ: Name: .{{z?}}debug_info
# CHECK-READOBJ: Name: .{{z?}}debug_aranges
# CHECK-READOBJ: Name: .{{z?}}debug_line

# CHECK-DWARFDUMP-INFO: .debug_info contents:
# CHECK-DWARFDUMP-INFO: 0x0000000b: DW_TAG_compile_unit
# CHECK-DWARFDUMP-INFO: 0x00000019: DW_TAG_compile_unit

# CHECK-DWARFDUMP-ABBREV: [1] DW_TAG_compile_unit  DW_CHILDREN_no
# CHECK-DWARFDUMP-ABBREV: [2] DW_TAG_compile_unit  DW_CHILDREN_no

# CHECK-DWARFDUMP-ARANGES: Address Range Header: {{.*}} cu_offset = 0x00000000,
# CHECK-DWARFDUMP-ARANGES: Address Range Header: {{.*}} cu_offset = 0x0000000e,


# Generated with:
# echo "int bar(); void _start() { bar(); }" | \
# clang++ -Os -g -S -x c - -Xclang -fdebug-compilation-dir -Xclang . -gdwarf-aranges -o -


  .text
  .file  "-"
  .globl  _start
_start:
.Lfunc_begin0:
  .file "<stdin>"
  xorl  %eax, %eax
  jmp  bar


  .section  .debug_str,"MS",@progbits,1
.Linfo_string3:
  .asciz  "_start"


  .section  .debug_abbrev,"",@progbits
  .byte  1                       # Abbreviation Code
  .byte  17                      # DW_TAG_compile_unit
  .byte  1                       # DW_CHILDREN_yes
  .byte  19                      # DW_AT_language
  .byte  5                       # DW_FORM_data2
  .byte  0                       # EOM(1)
  .byte  0                       # EOM(2)
  .byte  2                       # Abbreviation Code
  .byte  46                      # DW_TAG_subprogram
  .byte  0                       # DW_CHILDREN_no
  .byte  58                      # DW_AT_decl_file
  .byte  11                      # DW_FORM_data1
  .byte  59                      # DW_AT_decl_line
  .byte  11                      # DW_FORM_data1
  .byte  0                       # EOM(1)
  .byte  0                       # EOM(2)
  .byte  0                       # EOM(3)


  .section  .debug_info,"",@progbits
.Lcu_begin0:
  .long  .Ldebug_info_end0-.Ldebug_info_start0 # Length of Unit
.Ldebug_info_start0:
  .short  4                      # DWARF version number
  .long  .debug_abbrev           # Offset Into Abbrev. Section
  .byte  8                       # Address Size (in bytes)
  .byte  1                       # Abbrev [1] 0xb:0x35 DW_TAG_compile_unit
  .short  12                     # DW_AT_language
  .byte  2                       # Abbrev [2] 0x2a:0x15 DW_TAG_subprogram
  .byte  1                       # DW_AT_decl_file
  .byte  1                       # DW_AT_decl_line
                                 # DW_AT_external
  .byte  0                       # End Of Children Mark
.Ldebug_info_end0:
  .text
.Lsec_end0:


  .section  .debug_aranges,"",@progbits
  .long  44                      # Length of ARange Set
  .short  2                      # DWARF Arange version number
  .long  .Lcu_begin0             # Offset Into Debug Info Section
  .byte  8                       # Address Size (in bytes)
  .byte  0                       # Segment Size (in bytes)
  .zero  4,255
  .quad  .Lfunc_begin0
  .quad  .Lsec_end0-.Lfunc_begin0
  .quad  0                       # ARange terminator
  .quad  0
# facebook end T37438891
