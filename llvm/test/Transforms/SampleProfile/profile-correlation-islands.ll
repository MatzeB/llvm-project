; facebook T68973288
; RUN: opt < %s -passes=pseudo-probe,sample-profile -sample-profile-use-mcf -sample-profile-file=%S/Inputs/profile-correlation-islands.prof -S -o %t
; RUN: FileCheck %s < %t -check-prefix=CHECK-ENTRY-COUNT
; RUN: opt < %t -analyze -block-freq -enable-new-pm=0 | FileCheck %s

; The test contains an isolated flow component ("island") that needs to be
; reconnected to the entry point via edges with a positive flow.
; The corresponding CFG is shown below:
;
; +--------+     +--------+     +----------+
; | b6 [1] | <-- | b4 [1] | <-- |  b1 [1]  |
; +--------+     +--------+     +----------+
;                  |              |
;                  |              |
;                  v              v
;                +--------+     +----------+
;                | b5 [0] |     | b2 [100] | <+
;                +--------+     +----------+  |
;                                 |           |
;                                 |           |
;                                 v           |
;                               +----------+  |
;                               | b3 [100] | -+
;                               +----------+
;                                 |
;                                 |
;                                 v
;                               +----------+
;                               |  b7 [0]  |
;                               +----------+


; Function Attrs: nounwind uwtable
define dso_local i32 @islands_1(i32 %0, i32 %1) #0 {
b1:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 1, i32 0, i64 -1)
  %cmp = icmp ne i32 %0, 0
  br i1 %cmp, label %b2, label %b4
; CHECK: - b1: float = {{.*}}, int = {{.*}}, count = 2

b2:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 2, i32 0, i64 -1)
  br label %b3
; CHECK: - b2: float = {{.*}}, int = {{.*}}, count = 101

b3:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 3, i32 0, i64 -1)
  br i1 %cmp, label %b2, label %b7
; CHECK: - b3: float = {{.*}}, int = {{.*}}, count = 101

b4:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 4, i32 0, i64 -1)
  br i1 %cmp, label %b5, label %b6
; CHECK: - b4: float = {{.*}}, int = {{.*}}, count = 1

b5:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 5, i32 0, i64 -1)
  ret i32 %1
; CHECK: - b5: float = {{.*}}, int = {{.*}}, count = 0

b6:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 6, i32 0, i64 -1)
  ret i32 %1
; CHECK: - b6: float = {{.*}}, int = {{.*}}, count = 1

b7:
  call void @llvm.pseudoprobe(i64 -5646793257986063976, i64 7, i32 0, i64 -1)
  ret i32 %1
; CHECK: - b7: float = {{.*}}, int = {{.*}}, count = 1

}

declare void @llvm.pseudoprobe(i64, i64, i32, i64) #1

attributes #0 = { noinline nounwind uwtable "use-sample-profile"}
attributes #1 = { nounwind }

!llvm.pseudo_probe_desc = !{!7}

!7 = !{i64 -5646793257986063976, i64 120879332589, !"islands_1", null}

; CHECK-ENTRY-COUNT: = !{!"function_entry_count", i64 2}
