# facebook begin T37438891

# REQUIRES: x86
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t
# RUN: ld.lld %t -o %t2 --strip-debug-non-line
# RUN: llvm-readobj -sections -symbols %t2 | FileCheck %s

# CHECK: Name: .debug_aranges
# CHECK-NOT: Name: .debug_fram
# CHECK: Name: .debug_info
# CHECK: Name: .debug_line
# CHECK-NOT: Name: .debug_loc
# CHECK-NOT: Name: .debug_macinfo
# CHECK-NOT: Name: .debug_pubnames
# CHECK-NOT: Name: .debug_ranges
# CHECK: Name: .debug_str
# CHECK-NOT: Name: .debug_types
# CHECK: Name: .debug_gdb_scripts

# exits with return code 42 on linux
.pushsection .debug_aranges
.pushsection .debug_frame
.pushsection .debug_info
.pushsection .debug_line
.pushsection .debug_loc
.pushsection .debug_macinfo
.pushsection .debug_pubnames
.pushsection .debug_pubtypes
.pushsection .debug_ranges
.pushsection .debug_str
.pushsection .debut_types
.pushsection .debug_gdb_scripts
# facebook end T37438891
