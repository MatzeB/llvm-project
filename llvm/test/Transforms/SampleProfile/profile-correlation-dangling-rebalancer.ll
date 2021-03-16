; facebook T68973288
; RUN: opt < %s -passes=pseudo-probe,sample-profile -sample-profile-use-mcf -sample-profile-mcf-rebalance-dangling -sample-profile-file=%S/Inputs/profile-correlation-dangling-rebalancer.prof | opt -analyze -branch-prob -enable-new-pm=0 | FileCheck %s

; The test contains a "dimanond" and a "triangle" that needs to be rebalanced after MCF
; Original C++ code for the test case:
;
; #include <stdio.h>
;
; int countMultipliers(int n, int k) {
;   int mult = 0;
;   for (int i = 1; i <= n; i++) {
;     int a;
;     if (i % k) {
;       a = 0;
;     } else {
;       a = 1;
;     }
;
;     int b = 0;
;     if (i % k == 0) {
;       b = 1;
;     }
;     mult += a + b;
;   }
;   return mult / 2;
; }
;
; int main() {
;   int n = 100000;
;   int k = 3;
;   long int res = 0;
;   for (int i = 0; i < n; i++) {
;     res += countMultipliers(n, k);
;   }
;   res /= n;
;   printf("countMultipliers(%d, %d) = %ld\n", n, k, res);
;   return 0;
; }

; ModuleID = 'dangling.c'
source_filename = "dangling.c"

@yydebug = dso_local global i32 0, align 4

; Function Attrs: nounwind uwtable
define dso_local i32 @countMultipliers(i32 %0, i32 %1) #0 {
entry:
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 1, i32 0, i64 -1)
  %cmp = icmp ne i32 %0, 0
  br label %b11

b11:                                               ; preds = %b40, %b2
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 2, i32 0, i64 -1)
  br i1 %cmp, label %b17, label %b15

b15:                                               ; preds = %b11
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 3, i32 0, i64 -1)
  br label %b43

b17:                                               ; preds = %b11
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 4, i32 0, i64 -1)
  br i1 %cmp, label %b23, label %b24
; CHECK:  edge b17 -> b23 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b17 -> b24 probability is 0x40000000 / 0x80000000 = 50.00%

b23:                                               ; preds = %b17
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 5, i32 0, i64 -1)
  br label %b25

b24:                                               ; preds = %b17
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 6, i32 0, i64 -1)
  br label %b25

b25:                                               ; preds = %b24, %b23
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 7, i32 0, i64 -1)
  br i1 %cmp, label %b31, label %b32
; CHECK:  edge b25 -> b31 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b25 -> b32 probability is 0x40000000 / 0x80000000 = 50.00%

b31:                                               ; preds = %b25
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 8, i32 0, i64 -1)
  br label %b32

b32:                                               ; preds = %b31, %b25
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 9, i32 0, i64 -1)
  br label %b40

b40:                                               ; preds = %b32
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 10, i32 0, i64 -1)
  br label %b11

b43:                                               ; preds = %15
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 11, i32 0, i64 -1)
  ret i32 %1
}


; The test contains a triangle comprised of dangling probes.
;

define dso_local i32 @countMultipliers2(i32 %0, i32 %1) #0 {
b0:
  call void @llvm.pseudoprobe(i64 2506109673213838996, i64 1, i32 0, i64 -1)
  %cmp = icmp ne i32 %0, 0
  br i1 %cmp, label %b1, label %b5
; CHECK:  edge b0 -> b1 probability is 0x79e79e7a / 0x80000000 = 95.24% [HOT edge]
; CHECK:  edge b0 -> b5 probability is 0x06186186 / 0x80000000 = 4.76%

b1:
  call void @llvm.pseudoprobe(i64 2506109673213838996, i64 2, i32 0, i64 -1)
  br i1 %cmp, label %b2, label %b3
; CHECK:  edge b1 -> b2 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b1 -> b3 probability is 0x40000000 / 0x80000000 = 50.00%

b2:
  call void @llvm.pseudoprobe(i64 2506109673213838996, i64 3, i32 0, i64 -1)
  br i1 %cmp, label %b3, label %b4
; CHECK:  edge b2 -> b3 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b2 -> b4 probability is 0x40000000 / 0x80000000 = 50.00%

b3:
  call void @llvm.pseudoprobe(i64 2506109673213838996, i64 4, i32 0, i64 -1)
  br label %b5
; CHECK:  edge b3 -> b5 probability is 0x80000000 / 0x80000000 = 100.00% [HOT edge]

b4:
  call void @llvm.pseudoprobe(i64 2506109673213838996, i64 5, i32 0, i64 -1)
  br label %b5
; CHECK:  edge b4 -> b5 probability is 0x80000000 / 0x80000000 = 100.00% [HOT edge]

b5:
  call void @llvm.pseudoprobe(i64 2506109673213838996, i64 6, i32 0, i64 -1)
  ret i32 %1

}


; The test contains a dangling subgraph that contains an exit dangling block.
;

define dso_local i32 @countMultipliers3(i32 %0, i32 %1) #0 {
b31:
  call void @llvm.pseudoprobe(i64 -544905447084884130, i64 1, i32 0, i64 -1)
  br label %b32

b32:
  call void @llvm.pseudoprobe(i64 -544905447084884130, i64 2, i32 0, i64 -1)
  %cmp = icmp ne i32 %0, 0
  br i1 %cmp, label %b34, label %b33
; CHECK:  edge b32 -> b34 probability is 0x00000000 / 0x80000000 = 0.00%
; CHECK:  edge b32 -> b33 probability is 0x80000000 / 0x80000000 = 100.00% [HOT edge]

b33:
  call void @llvm.pseudoprobe(i64 -544905447084884130, i64 3, i32 0, i64 -1)
  ret i32 %1

b34:
  call void @llvm.pseudoprobe(i64 -544905447084884130, i64 4, i32 0, i64 -1)
  ret i32 %1

}


define dso_local i32 @countMultipliers4(i32 %0, i32 %1) #0 {
b40:
  call void @llvm.pseudoprobe(i64 -2989539179265513123, i64 1, i32 0, i64 -1)
  %cmp = icmp ne i32 %0, 0
  br i1 %cmp, label %b41, label %b42

b41:
  call void @llvm.pseudoprobe(i64 -2989539179265513123, i64 2, i32 0, i64 -1)
  br label %b43

b42:
  call void @llvm.pseudoprobe(i64 -2989539179265513123, i64 3, i32 0, i64 -1)
  br i1 %cmp, label %b43, label %b44
; CHECK:  edge b42 -> b43 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b42 -> b44 probability is 0x40000000 / 0x80000000 = 50.00%

b43:
  call void @llvm.pseudoprobe(i64 -2989539179265513123, i64 4, i32 0, i64 -1)
  br label %b44

b44:
  call void @llvm.pseudoprobe(i64 -2989539179265513123, i64 5, i32 0, i64 -1)
  ret i32 %1

}

; Function Attrs: inaccessiblememonly nounwind willreturn
declare void @llvm.pseudoprobe(i64, i64, i32, i64) #4

attributes #0 = { noinline nounwind uwtable "use-sample-profile" }
attributes #4 = { inaccessiblememonly nounwind willreturn }

!llvm.pseudo_probe_desc = !{!7, !8, !9, !10}

!7 = !{i64 -5758218299531803684, i64 223598586707, !"countMultipliers", null}
!8 = !{i64 2506109673213838996, i64 2235985, !"countMultipliers2", null}
!9 = !{i64 -544905447084884130, i64 22985, !"countMultipliers3", null}
!10 = !{i64 -2989539179265513123, i64 2298578, !"countMultipliers4", null}
