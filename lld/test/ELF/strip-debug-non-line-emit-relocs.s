# facebook begin D16076751
# REQUIRES: x86
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t.o
# RUN: ld.lld %t.o -o %t --strip-debug-non-line --emit-relocs
# RUN: llvm-readobj --sections --symbols %t | FileCheck %s --check-prefix=CHECK-READOBJ
# RUN: llvm-dwarfdump --verify %t
# RUN: llvm-dwarfdump --debug-info %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-INFO
# RUN: llvm-dwarfdump --debug-abbrev %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-ABBREV
# RUN: llvm-dwarfdump --debug-aranges %t | FileCheck %s --check-prefix=CHECK-DWARFDUMP-ARANGES

# TODO: when --emit-relocs is used with --strip-debug-non-line the
# .rela.debug_info/.rel.debug_info need to be updated. That's not implemented yet.
# So when both flags are set .debug_info is not reduced and --strip-debug-non-line only
# removes full sections (and their associalted .rela/.rel sections).

# CHECK-READOBJ: Name: .debug_abbrev ({{[0-9]+}})
# CHECK-READOBJ: Name: .debug_info ({{[0-9]+}})
# CHECK-READOBJ: Name: .rela.debug_info ({{[0-9]+}})
# CHECK-READOBJ: Name: .debug_aranges ({{[0-9]+}})
# CHECK-READOBJ: Name: .rela.debug_aranges ({{[0-9]+}})
# CHECK-READOBJ: Name: .debug_str ({{[0-9]+}})
# CHECK-READOBJ: Name: .debug_line ({{[0-9]+}})
# CHECK-READOBJ: Name: .rela.debug_line ({{[0-9]+}})

# CHECK-DWARFDUMP-INFO: .debug_info contents:
# CHECK-DWARFDUMP-INFO: 0x0000000b: DW_TAG_compile_unit

# CHECK-DWARFDUMP-ABBREV: [1] DW_TAG_compile_unit  DW_CHILDREN_yes

# CHECK-DWARFDUMP-ARANGES: Address Range Header: {{.*}} cu_offset = 0x00000000,


# Generated with:
# echo "int foo() { return 0; } void _start() { foo(); }" | \
# clang++ -Os -g -S -x c - -Xclang -fdebug-compilation-dir -Xclang . -gdwarf-aranges -o -

    .text
    .file   "-"
    .globl  foo                             # -- Begin function foo
    .type   foo,@function
foo:                                    # @foo
.Lfunc_begin0:
    .file   1 "." "<stdin>"
    .loc    1 1 0                           # <stdin>:1:0
    .cfi_startproc
# %bb.0:
    .loc    1 1 13 prologue_end             # <stdin>:1:13
    xorl    %eax, %eax
    retq
.Ltmp0:
.Lfunc_end0:
    .size   foo, .Lfunc_end0-foo
    .cfi_endproc
                                        # -- End function
    .globl  _start                          # -- Begin function _start
    .type   _start,@function
_start:                                 # @_start
.Lfunc_begin1:
    .loc    1 1 0                           # <stdin>:1:0
    .cfi_startproc
# %bb.0:
    .loc    1 1 48 prologue_end             # <stdin>:1:48
    retq
.Ltmp1:
.Lfunc_end1:
    .size   _start, .Lfunc_end1-_start
    .cfi_endproc
                                        # -- End function
    .section    .debug_abbrev,"",@progbits
    .byte   1                               # Abbreviation Code
    .byte   17                              # DW_TAG_compile_unit
    .byte   1                               # DW_CHILDREN_yes
    .byte   37                              # DW_AT_producer
    .byte   14                              # DW_FORM_strp
    .byte   19                              # DW_AT_language
    .byte   5                               # DW_FORM_data2
    .byte   3                               # DW_AT_name
    .byte   14                              # DW_FORM_strp
    .byte   16                              # DW_AT_stmt_list
    .byte   23                              # DW_FORM_sec_offset
    .byte   27                              # DW_AT_comp_dir
    .byte   14                              # DW_FORM_strp
    .byte   17                              # DW_AT_low_pc
    .byte   1                               # DW_FORM_addr
    .byte   18                              # DW_AT_high_pc
    .byte   6                               # DW_FORM_data4
    .byte   0                               # EOM(1)
    .byte   0                               # EOM(2)
    .byte   2                               # Abbreviation Code
    .byte   46                              # DW_TAG_subprogram
    .byte   0                               # DW_CHILDREN_no
    .byte   17                              # DW_AT_low_pc
    .byte   1                               # DW_FORM_addr
    .byte   18                              # DW_AT_high_pc
    .byte   6                               # DW_FORM_data4
    .byte   64                              # DW_AT_frame_base
    .byte   24                              # DW_FORM_exprloc
    .ascii  "\227B"                         # DW_AT_GNU_all_call_sites
    .byte   25                              # DW_FORM_flag_present
    .byte   3                               # DW_AT_name
    .byte   14                              # DW_FORM_strp
    .byte   58                              # DW_AT_decl_file
    .byte   11                              # DW_FORM_data1
    .byte   59                              # DW_AT_decl_line
    .byte   11                              # DW_FORM_data1
    .byte   73                              # DW_AT_type
    .byte   19                              # DW_FORM_ref4
    .byte   63                              # DW_AT_external
    .byte   25                              # DW_FORM_flag_present
    .byte   0                               # EOM(1)
    .byte   0                               # EOM(2)
    .byte   3                               # Abbreviation Code
    .byte   46                              # DW_TAG_subprogram
    .byte   0                               # DW_CHILDREN_no
    .byte   17                              # DW_AT_low_pc
    .byte   1                               # DW_FORM_addr
    .byte   18                              # DW_AT_high_pc
    .byte   6                               # DW_FORM_data4
    .byte   64                              # DW_AT_frame_base
    .byte   24                              # DW_FORM_exprloc
    .ascii  "\227B"                         # DW_AT_GNU_all_call_sites
    .byte   25                              # DW_FORM_flag_present
    .byte   3                               # DW_AT_name
    .byte   14                              # DW_FORM_strp
    .byte   58                              # DW_AT_decl_file
    .byte   11                              # DW_FORM_data1
    .byte   59                              # DW_AT_decl_line
    .byte   11                              # DW_FORM_data1
    .byte   63                              # DW_AT_external
    .byte   25                              # DW_FORM_flag_present
    .byte   0                               # EOM(1)
    .byte   0                               # EOM(2)
    .byte   4                               # Abbreviation Code
    .byte   36                              # DW_TAG_base_type
    .byte   0                               # DW_CHILDREN_no
    .byte   3                               # DW_AT_name
    .byte   14                              # DW_FORM_strp
    .byte   62                              # DW_AT_encoding
    .byte   11                              # DW_FORM_data1
    .byte   11                              # DW_AT_byte_size
    .byte   11                              # DW_FORM_data1
    .byte   0                               # EOM(1)
    .byte   0                               # EOM(2)
    .byte   0                               # EOM(3)
    .section    .debug_info,"",@progbits
.Lcu_begin0:
    .long   .Ldebug_info_end0-.Ldebug_info_start0 # Length of Unit
.Ldebug_info_start0:
    .short  4                               # DWARF version number
    .long   .debug_abbrev                   # Offset Into Abbrev. Section
    .byte   8                               # Address Size (in bytes)
    .byte   1                               # Abbrev [1] 0xb:0x55 DW_TAG_compile_unit
    .long   .Linfo_string0                  # DW_AT_producer
    .short  12                              # DW_AT_language
    .long   .Linfo_string1                  # DW_AT_name
    .long   .Lline_table_start0             # DW_AT_stmt_list
    .long   .Linfo_string2                  # DW_AT_comp_dir
    .quad   .Lfunc_begin0                   # DW_AT_low_pc
    .long   .Lfunc_end1-.Lfunc_begin0       # DW_AT_high_pc
    .byte   2                               # Abbrev [2] 0x2a:0x19 DW_TAG_subprogram
    .quad   .Lfunc_begin0                   # DW_AT_low_pc
    .long   .Lfunc_end0-.Lfunc_begin0       # DW_AT_high_pc
    .byte   1                               # DW_AT_frame_base
    .byte   87
                                        # DW_AT_GNU_all_call_sites
    .long   .Linfo_string3                  # DW_AT_name
    .byte   1                               # DW_AT_decl_file
    .byte   1                               # DW_AT_decl_line
    .long   88                              # DW_AT_type
                                        # DW_AT_external
    .byte   3                               # Abbrev [3] 0x43:0x15 DW_TAG_subprogram
    .quad   .Lfunc_begin1                   # DW_AT_low_pc
    .long   .Lfunc_end1-.Lfunc_begin1       # DW_AT_high_pc
    .byte   1                               # DW_AT_frame_base
    .byte   87
                                        # DW_AT_GNU_all_call_sites
    .long   .Linfo_string5                  # DW_AT_name
    .byte   1                               # DW_AT_decl_file
    .byte   1                               # DW_AT_decl_line
                                        # DW_AT_external
    .byte   4                               # Abbrev [4] 0x58:0x7 DW_TAG_base_type
    .long   .Linfo_string4                  # DW_AT_name
    .byte   5                               # DW_AT_encoding
    .byte   4                               # DW_AT_byte_size
    .byte   0                               # End Of Children Mark
.Ldebug_info_end0:
    .text
.Lsec_end0:
    .section    .debug_aranges,"",@progbits
    .long   44                              # Length of ARange Set
    .short  2                               # DWARF Arange version number
    .long   .Lcu_begin0                     # Offset Into Debug Info Section
    .byte   8                               # Address Size (in bytes)
    .byte   0                               # Segment Size (in bytes)
    .zero   4,255
    .quad   .Lfunc_begin0
    .quad   .Lsec_end0-.Lfunc_begin0
    .quad   0                               # ARange terminator
    .quad   0
    .section    .debug_str,"MS",@progbits,1
.Linfo_string0:
    .asciz  "clang version 12.0.0 (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/llvm-project 1049b33aa045ae36771a7924c52679bf94dc5b7e)" # string offset=0
.Linfo_string1:
    .asciz  "-"                             # string offset=134
.Linfo_string2:
    .asciz  "."                             # string offset=136
.Linfo_string3:
    .asciz  "foo"                           # string offset=138
.Linfo_string4:
    .asciz  "int"                           # string offset=142
.Linfo_string5:
    .asciz  "_start"                        # string offset=146
    .ident  "clang version 12.0.0 (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/llvm-project 1049b33aa045ae36771a7924c52679bf94dc5b7e)"
    .section    ".note.GNU-stack","",@progbits
    .addrsig
    .section    .debug_line,"",@progbits
.Lline_table_start0:
# facebook end D16076751
