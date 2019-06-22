# facebook begin T37438891
# REQUIRES: x86
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t.o
# RUN: ld.lld %t.o -o %t --strip-debug-non-line
# RUN: llvm-readobj --sections --symbols %t | FileCheck %s --check-prefix=CHECK-READOBJ
# RUN: llvm-dwarfdump --verify %t
# RUN: llvm-dwarfdump --debug-info %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-INFO
# RUN: llvm-dwarfdump --debug-abbrev %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-ABBREV
# RUN: llvm-dwarfdump --debug-aranges %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-ARANGES

# CHECK-READOBJ: Name: .{{z?}}debug_str_offsets
# CHECK-READOBJ: Name: .{{z?}}debug_str
# CHECK-READOBJ: Name: .{{z?}}debug_abbrev
# CHECK-READOBJ: Name: .{{z?}}debug_info
# CHECK-READOBJ: Name: .{{z?}}debug_aranges
# CHECK-READOBJ: Name: .{{z?}}debug_addr
# CHECK-READOBJ: Name: .{{z?}}debug_line

# CHECK-DWARFDUMP-INFO: .debug_info contents:
# CHECK-DWARFDUMP-INFO: 0x0000000c: DW_TAG_compile_unit

# CHECK-DWARFDUMP-ABBREV: [1] DW_TAG_compile_unit  DW_CHILDREN_no

# CHECK-DWARFDUMP-ARANGES: Address Range Header: {{.*}} cu_offset = 0x00000000,


    .text
    .file    "-"
    .globl    foo
foo:
.Lfunc_begin0:
    .file   "<stdin>"
    xorl    %eax, %eax
    retq

    .globl    _start
    .type    _start,@function
_start:
.Lfunc_begin1:
    retq


    .section    .debug_str_offsets,"",@progbits
    .long    28
    .short    5
    .short    0
.Lstr_offsets_base0:


    .section    .debug_str,"MS",@progbits,1
.Linfo_string4:
    .asciz    "int"
.Linfo_string5:
    .asciz    "_start"
    .section    .debug_str_offsets,"",@progbits
    .long    .Linfo_string4
    .long    .Linfo_string5


    .section    .debug_abbrev,"",@progbits
    .byte    1                       # Abbreviation Code
    .byte    17                      # DW_TAG_compile_unit
    .byte    1                       # DW_CHILDREN_yes
    .byte    19                      # DW_AT_language
    .byte    5                       # DW_FORM_data2
    .byte    0                       # EOM(1)
    .byte    0                       # EOM(2)
    .byte    2                       # Abbreviation Code
    .byte    46                      # DW_TAG_subprogram
    .byte    0                       # DW_CHILDREN_no
    .byte    58                      # DW_AT_decl_file
    .byte    11                      # DW_FORM_data1
    .byte    59                      # DW_AT_decl_line
    .byte    11                      # DW_FORM_data1
    .byte    0                       # EOM(1)
    .byte    0                       # EOM(2)
    .byte    3                       # Abbreviation Code
    .byte    46                      # DW_TAG_subprogram
    .byte    0                       # DW_CHILDREN_no
    .byte    58                      # DW_AT_decl_file
    .byte    11                      # DW_FORM_data1
    .byte    59                      # DW_AT_decl_line
    .byte    11                      # DW_FORM_data1
    .byte    0                       # EOM(1)
    .byte    0                       # EOM(2)
    .byte    4                       # Abbreviation Code
    .byte    36                      # DW_TAG_base_type
    .byte    0                       # DW_CHILDREN_no
    .byte    3                       # DW_AT_name
    .byte    37                      # DW_FORM_strx1
    .byte    62                      # DW_AT_encoding
    .byte    11                      # DW_FORM_data1
    .byte    11                      # DW_AT_byte_size
    .byte    11                      # DW_FORM_data1
    .byte    0                       # EOM(1)
    .byte    0                       # EOM(2)
    .byte    0                       # EOM(3)


    .section    .debug_info,"",@progbits
.Lcu_begin0:
    .long    .Ldebug_info_end0-.Ldebug_info_start0 # Length of Unit
.Ldebug_info_start0:
    .short    5                      # DWARF version number
    .byte    1                       # DWARF Unit Type
    .byte    8                       # Address Size (in bytes)
    .long    .debug_abbrev           # Offset Into Abbrev. Section
    .byte    1                       # Abbrev [1] 0xc:0x36 DW_TAG_compile_unit
    .short    12                     # DW_AT_language
    .byte    2                       # Abbrev [2] 0x23:0xf DW_TAG_subprogram
    .byte    1                       # DW_AT_decl_file
    .byte    1                       # DW_AT_decl_line
    .byte    3                       # Abbrev [3] 0x32:0xb DW_TAG_subprogram
    .byte    1                       # DW_AT_decl_file
    .byte    1                       # DW_AT_decl_line
    .byte    4                       # Abbrev [4] 0x3d:0x4 DW_TAG_base_type
    .byte    4                       # DW_AT_name
    .byte    5                       # DW_AT_encoding
    .byte    4                       # DW_AT_byte_size
    .byte    0                       # End Of Children Mark
.Ldebug_info_end0:
    .text
.Lsec_end0:


    .section    .debug_aranges,"",@progbits
    .long    44                      # Length of ARange Set
    .short    2                      # DWARF Arange version number
    .long    .Lcu_begin0             # Offset Into Debug Info Section
    .byte    8                       # Address Size (in bytes)
    .byte    0                       # Segment Size (in bytes)
    .zero    4,255
    .quad    .Lfunc_begin0
    .quad    .Lsec_end0-.Lfunc_begin0
    .quad    0                       # ARange terminator
    .quad    0


    .section    .debug_addr,"",@progbits
    .long    .Ldebug_addr_end0-.Ldebug_addr_start0 # Length of contribution
.Ldebug_addr_start0:
    .short    5                      # DWARF version number
    .byte    8                       # Address size
    .byte    0                       # Segment selector size
    .quad    .Lfunc_begin0
    .quad    .Lfunc_begin1
.Ldebug_addr_end0:
# facebook end T37438891
