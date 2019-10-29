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
# RUN: ld.lld -r --discard-section .section_one --discard-section .section_two %t3 -o %t4 2>&1 | FileCheck -check-prefix=WARN %s
# RUN: llvm-readobj --sections --symbols %t4 | FileCheck %s -check-prefix=NOTREMOVED

## Attempt to use --emit-relocs (-q)  and --discard-section together
# RUN: llvm-mc -g -filetype=obj -triple=x86_64-unknown-linux %s -o %t5
# RUN: ld.lld --emit-relocs --discard-section .section_one --discard-section .section_two %t5 -o %t6 2>&1 | FileCheck -check-prefix=WARN %s
# RUN: llvm-readobj --sections --symbols %t6 | FileCheck %s -check-prefix=NOTREMOVED

# WARN: warning: -r or -q may not be used with --discard-sections

# NOTREMOVED: Name: .section_one
# NOTREMOVED: Name: .section_two
# NOTREMOVED: Name: .section_three

.pushsection .section_one
.pushsection .section_two
.pushsection .section_three
# facebook end T46459577
