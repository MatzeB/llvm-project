; RUN: llc -mtriple=aarch64-none-linux-gnu -aarch64-min-jump-table-entries=4 \
; RUN:   -aarch64-enable-atomic-cfg-tidy=0 -filetype=asm %s -o - | FileCheck %s

define i32 @test_jumptable(i32 %in) "emit-jump-table-info" {
entry:
  switch i32 %in, label %def [
    i32 0, label %lbl1
    i32 1, label %lbl2
    i32 2, label %lbl3
    i32 4, label %lbl4
  ]

def:
  ret i32 0

lbl1:
  ret i32 1

lbl2:
  ret i32 2

lbl3:
  ret i32 4

lbl4:
  ret i32 8
}

; CHECK-LABEL: test_jumptable:
; CHECK: [[REF1:.Ltmp[0-9]+]]:
; CHECK: [[REF2:.Ltmp[0-9]+]]:
; CHECK: [[LOAD:.Ltmp[0-9]+]]:
; CHECK: [[ADD:.Ltmp[0-9]+]]:
; CHECK: [[BRANCH:.Ltmp[0-9]+]]:
; CHECK: [[JT:.LJTI[0-9]+_[0-9]+]]:
; CHECK: .byte ([[BASE:.LBB[0-9]+_[0-9]+]]-[[BASE]])>>2
; CHECK: .section .llvm_jump_table_info,"o",@0x6fff4c0e,test_jumptable
; CHECK-NEXT: .byte{{[[:space:]]+}}2{{.*}}format 2: 1b relative; shr 2
; CHECK-NEXT: .byte{{[[:space:]]+}}58{{.*}}Record Content Length
; CHECK-NEXT: .xword{{[[:space:]]+}}[[JT]]
; CHECK-NEXT: .xword{{[[:space:]]+}}[[BASE]]{{.*}}Base
; CHECK-NEXT: .xword{{[[:space:]]+}}[[LOAD]]{{.*}}Load Instruction
; CHECK-NEXT: .xword{{[[:space:]]+}}[[ADD]]{{.*}}Add Instruction
; CHECK-NEXT: .xword{{[[:space:]]+}}[[BRANCH]]{{.*}}Branch Instruction
; CHECK-NEXT: .byte{{[[:space:]]+}}5{{.*}}Number of Entries
; CHECK-NEXT: .byte{{[[:space:]]+}}2{{.*}}Number of References
; CHECK-NEXT: .xword{{[[:space:]]+}}[[REF1]]{{.*}}Reference
; CHECK-NEXT: .xword{{[[:space:]]+}}[[REF2]]{{.*}}Reference
