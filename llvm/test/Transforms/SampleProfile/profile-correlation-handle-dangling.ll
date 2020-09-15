; facebook T68973288
; RUN: opt < %s -passes=pseudo-probe,sample-profile -sample-profile-use-mcf -sample-profile-file=%S/Inputs/profile-correlation-handle-dangling.prof | opt -analyze -branch-prob -enable-new-pm=0 | FileCheck %s

@yydebug = dso_local global i32 0, align 4

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @yyparse_1() #0 {
entry:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 1, i32 0, i64 -1)
  %0 = load i32, i32* @yydebug, align 4
  %cmp = icmp ne i32 %0, 0
  br i1 %cmp, label %if.true1, label %if.false1
; CHECK:  edge entry -> if.true1 probability is 0x73333333 / 0x80000000 = 90.00% [HOT edge]
; CHECK:  edge entry -> if.false1 probability is 0x0ccccccd / 0x80000000 = 10.00%

if.true1:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 2, i32 0, i64 -1)
  br label %if.end1

if.false1:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 3, i32 0, i64 -1)
  br label %if.end1

if.end1:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 4, i32 0, i64 -1)
  br i1 %cmp, label %if.true2, label %if.false2
; CHECK:  edge if.end1 -> if.true2 probability is 0x0ccccccd / 0x80000000 = 10.00%
; CHECK:  edge if.end1 -> if.false2 probability is 0x73333333 / 0x80000000 = 90.00% [HOT edge]

if.true2:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 5, i32 0, i64 -1)
  br label %if.end2

if.false2:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 6, i32 0, i64 -1)
  br label %if.end2

if.end2:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 7, i32 0, i64 -1)
  br i1 %cmp, label %if.true3, label %if.false3
; CHECK:  edge if.end2 -> if.true3 probability is 0x80000000 / 0x80000000 = 100.00% [HOT edge]
; CHECK:  edge if.end2 -> if.false3 probability is 0x00000000 / 0x80000000 = 0.00%

if.true3:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 8, i32 0, i64 -1)
  br label %if.end3

if.false3:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 9, i32 0, i64 -1)
  br label %if.end3

if.end3:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 10, i32 0, i64 -1)
  br i1 %cmp, label %if.true4, label %if.false4
; CHECK:  edge if.end3 -> if.true4 probability is 0x00000000 / 0x80000000 = 0.00%
; CHECK:  edge if.end3 -> if.false4 probability is 0x80000000 / 0x80000000 = 100.00% [HOT edge]

if.true4:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 11, i32 0, i64 -1)
  br label %exit

if.false4:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 12, i32 0, i64 -1)
  br label %exit

exit:
  call void @llvm.pseudoprobe(i64 -7702751003264189226, i64 13, i32 0, i64 -1)
  %1 = load i32, i32* @yydebug, align 4
  ret i32 %1
}

declare void @llvm.pseudoprobe(i64, i64, i32, i64) #1

attributes #0 = { noinline nounwind uwtable "use-sample-profile"}
attributes #1 = { nounwind }

!llvm.pseudo_probe_desc = !{!1079}
!1079 = !{i64 -7702751003264189226, i64 158496288380146391, !"yyparse_1", null}
