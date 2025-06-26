# facebook begin T221560075
# REQUIRES: x86

## Test discarding .nvFatBinSegment with --emit-relocs
# RUN: llvm-mc -x86-asm-syntax=intel -filetype=obj -triple=x86_64-unknown-linux %s -o %t1.o
# RUN: ld.lld --hash-style=sysv --emit-relocs %t1.o -o %t1.out --discard-section=.nvFatBinSegment
# RUN: llvm-readobj --sections --symbols %t1.out | FileCheck %s --check-prefix=DISCARD1

# DISCARD1-NOT: Name: .nvFatBinSegment
# DISCARD1-DAG: Name: .nv_fatbin
# DISCARD1-DAG: Name: __nv_module_id
# DISCARD1-DAG: Name: .rela.nv_fatbin

## Test discarding .nv_fatbin with --emit-relocs
# RUN: llvm-mc -x86-asm-syntax=intel -filetype=obj -triple=x86_64-unknown-linux %s -o %t2.o
# RUN: ld.lld --hash-style=sysv --emit-relocs %t2.o -o %t2.out --discard-section=.nv_fatbin
# RUN: llvm-readobj --sections --symbols %t2.out | FileCheck %s --check-prefix=DISCARD2

# DISCARD2-DAG: Name: .nvFatBinSegment
# DISCARD2-NOT: Name: .nv_fatbin
# DISCARD2-DAG: Name: __nv_module_id
# DISCARD2-NOT: Name: .rela.nv_fatbin

## Test discarding __nv_module_id with --emit-relocs
# RUN: llvm-mc -x86-asm-syntax=intel -filetype=obj -triple=x86_64-unknown-linux %s -o %t3.o
# RUN: ld.lld --hash-style=sysv --emit-relocs %t3.o -o %t3.out --discard-section=__nv_module_id
# RUN: llvm-readobj --sections --symbols %t3.out | FileCheck %s --check-prefix=DISCARD3

# DISCARD3-DAG: Name: .nvFatBinSegment
# DISCARD3-DAG: Name: .nv_fatbin
# DISCARD3-NOT: Name: __nv_module_id
# DISCARD3-DAG: Name: .rela.nv_fatbin

.section .nvFatBinSegment
.global global_sym
global_sym: .quad 1
local_sym: .quad 2

.section .nv_fatbin
.global global_sym_ref
global_sym_ref: .quad global_sym
local_sym_ref: .quad local_sym
.global global_sym_ref_local
global_sym_ref_local: .quad local_sym

.section __nv_module_id
.global global_func
global_func:
    ret
local_func:
    ret

.section .text
.global _start
_start:
    mov rax, [global_sym]
    mov rdi, [global_sym_ref]
    mov rsi, [local_sym]
    mov rax, [local_sym_ref]
    mov rdi, [global_sym_ref_local]
    call global_func
    call local_func
    mov rax, 60
    xor rdi, rdi
    syscall
# facebook end T221560075
