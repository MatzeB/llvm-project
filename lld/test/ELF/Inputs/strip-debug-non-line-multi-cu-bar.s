# facebook begin T37438891
# Generated with:
# echo "int bar() { return 0; }" | \
# clang++ -Os -g -S -x c - -Xclang -fdebug-compilation-dir -Xclang . -gdwarf-aranges -o -


  .text
  .file  "-"
  .globl  bar
bar:
.Lfunc_begin0:
  .file "<stdin>"
  xorl  %eax, %eax
  retq


  .section  .debug_str,"MS",@progbits,1
.Linfo_string4:
  .asciz  "int"


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
  .byte  3                       # Abbreviation Code
  .byte  36                      # DW_TAG_base_type
  .byte  0                       # DW_CHILDREN_no
  .byte  3                       # DW_AT_name
  .byte  14                      # DW_FORM_strp
  .byte  62                      # DW_AT_encoding
  .byte  11                      # DW_FORM_data1
  .byte  11                      # DW_AT_byte_size
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
  .byte  1                       # Abbrev [1] 0xb:0x40 DW_TAG_compile_unit
  .short  12                     # DW_AT_language
  .byte  2                       # Abbrev [2] 0x2a:0x19 DW_TAG_subprogram
  .byte  1                       # DW_AT_decl_file
  .byte  1                       # DW_AT_decl_line
  .byte  3                       # Abbrev [3] 0x43:0x7 DW_TAG_base_type
  .long  .Linfo_string4          # DW_AT_name
  .byte  5                       # DW_AT_encoding
  .byte  4                       # DW_AT_byte_size
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
