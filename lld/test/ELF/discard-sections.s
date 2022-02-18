# facebook begin T46459577

# REQUIRES: x86
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t
# RUN: ld.lld %t -o %t2 --discard-section .section_one --discard-section .section_two
# RUN: llvm-readobj --sections --symbols %t2 | FileCheck %s -check-prefix=REMOVED

# REMOVED-NOT: Name: .section_one
# REMOVED-NOT: Name: .section_two
# REMOVED: Name: .section_three

# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t
# RUN: ld.lld %t -o %t2 --no-discard-section .section_one --discard-section .section_two --discard-section .section_one --no-discard-section .section_two
# RUN: llvm-readobj --sections --symbols %t2 | FileCheck %s -check-prefix=NODISCARD

# NODISCARD-NOT: Name: .section_one
# NODISCARD: Name: .section_two
# NODISCARD: Name: .section_three

## Attempt to use -r and --discard-section together
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t3
# RUN: ld.lld -r --discard-section .section_one --discard-section .section_two --discard-section .non_DWARF_debug_section --discard-section .debug_line %t3 -o %t4 2>&1 | FileCheck -check-prefix=WARNRELOCS %s
# RUN: llvm-readobj --sections --symbols %t4 | FileCheck %s -check-prefix=RELOCS

# WARNRELOCS: warning: -r may not be used with --discard-section, ignoring all instances of --discard-section

# RELOCS: Name: .section_one
# RELOCS: Name: .section_two
# RELOCS: Name: .section_three
# RELOCS: Name: .non_DWARF_debug_section
# RELOCS: Name: .debug_line

## Attempt to use --emit-relocs (-q)  and --discard-section together
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t5
# RUN: ld.lld --emit-relocs --discard-section .section_one --discard-section .section_two --discard-section .non_DWARF_debug_section --discard-section .debug_line %t5 -o %t6 2>&1 | FileCheck -check-prefix=WARNEMIT %s
# RUN: llvm-readobj --sections --symbols %t6 | FileCheck %s -check-prefix=EMIT

# WARNEMIT: warning: --emit-relocs may not be used with --discard-section unless section is a debug section, ignoring --discard-section .section_one
# WARNEMIT: warning: --emit-relocs may not be used with --discard-section unless section is a debug section, ignoring --discard-section .section_two
# WARNEMIT: warning: --emit-relocs may not be used with --discard-section unless section is a debug section, ignoring --discard-section .non_DWARF_debug_section
# WARNRELOCS-NOT: warning: --emit-relocs may not be used with --discard-section unless section is a debug section, ignoring --discard-section .debug_line

# EMIT: Name: .section_one
# EMIT: Name: .section_two
# EMIT: Name: .section_three
# EMIT: Name: .non_DWARF_debug_section
# EMIT-NOT: Name: .debug_line

## Check that we can discard .debug_ independently from .rela.debug_
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t7
# RUN: ld.lld --emit-relocs --discard-section .debug_section  %t7 -o %t8 2>&1
# RUN: llvm-readobj --sections --symbols %t8 | FileCheck %s -check-prefix=DEBUGONLY

# RUN: ld.lld --emit-relocs --discard-section .rela.debug_section  %t7 -o %t9
# RUN: llvm-readobj --sections --symbols %t9 | FileCheck %s -check-prefix=RELAONLY

# RUN: ld.lld --emit-relocs --discard-section .debug_section --discard-section .rela.debug_section  %t7 -o %t10
# RUN: llvm-readobj --sections --symbols %t10 | FileCheck %s -check-prefix=BOTH

# DEBUGONLY: Name: .rela.debug_section
# DEBUGONLY-NOT: Name: .debug_section

# RELAONLY: Name: .debug_section
# RELAGONLY-NOT: Name: .rela.debug_section

# BOTH-NOT: Name: .debug_section
# BOTH-NOT: Name: .rela.debug_section

.pushsection .section_one
.pushsection .section_two
.pushsection .section_three
.pushsection .non_DWARF_debug_section
.pushsection .rela.debug_section
.pushsection .debug_section
# facebook end T46459577
