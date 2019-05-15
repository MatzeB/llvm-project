; facebook T44360418
; RUN: opt < %s 2>&1 -disable-output \
; RUN: 	   -passes=inline -print-after-all -filter-print-funcs=foo -print-for-dev | \
; RUN:     FileCheck %s --implicit-check-not="call void @llvm.dbg" --implicit-check-not="DILocation"

; CHECK: IR Dump After InlinerPass
; CHECK: define dso_local i32 @foo
; CHECK: [ tiny.c:2:3 ]
; CHECK: [ tiny.c:2:10 ]

; ModuleID = 'tiny.c'
source_filename = "tiny.c"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @foo(i32) #0 !dbg !7 {
  %2 = alloca i32, align 4
  store i32 %0, i32* %2, align 4
  call void @llvm.dbg.declare(metadata i32* %2, metadata !11, metadata !DIExpression()), !dbg !12
  br label %3, !dbg !13

3:                                                ; preds = %6, %1
  %4 = load i32, i32* %2, align 4, !dbg !14
  %5 = icmp sgt i32 %4, 15, !dbg !15
  br i1 %5, label %6, label %9, !dbg !13

6:                                                ; preds = %3
  %7 = load i32, i32* %2, align 4, !dbg !16
  %8 = shl i32 %7, 1, !dbg !16
  store i32 %8, i32* %2, align 4, !dbg !16
  br label %3, !dbg !13, !llvm.loop !18

9:                                                ; preds = %3
  %10 = load i32, i32* %2, align 4, !dbg !20
  ret i32 %10, !dbg !21
}

; Function Attrs: nounwind readnone speculatable
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

attributes #0 = { noinline nounwind optnone uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" "use-sample-profile" }
attributes #1 = { nounwind readnone speculatable }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 9.0.0 (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/clang fe04b9e92925068ad84279fa4a92fa1c55056080) (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/llvm a163dd8b8c98f0d56bc6eb9c5b78855e3f53265b)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, nameTableKind: None)
!1 = !DIFile(filename: "tiny.c", directory: "/home/wenlei/local/autofdo/inline")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 9.0.0 (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/clang fe04b9e92925068ad84279fa4a92fa1c55056080) (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/llvm a163dd8b8c98f0d56bc6eb9c5b78855e3f53265b)"}
!7 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!8 = !DISubroutineType(types: !9)
!9 = !{!10, !10}
!10 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!11 = !DILocalVariable(name: "x", arg: 1, scope: !7, file: !1, line: 1, type: !10)
!12 = !DILocation(line: 1, column: 13, scope: !7)
!13 = !DILocation(line: 2, column: 3, scope: !7)
!14 = !DILocation(line: 2, column: 10, scope: !7)
!15 = !DILocation(line: 2, column: 12, scope: !7)
!16 = !DILocation(line: 3, column: 7, scope: !17)
!17 = distinct !DILexicalBlock(scope: !7, file: !1, line: 2, column: 19)
!18 = distinct !{!18, !13, !19}
!19 = !DILocation(line: 4, column: 3, scope: !7)
!20 = !DILocation(line: 5, column: 10, scope: !7)
!21 = !DILocation(line: 5, column: 3, scope: !7)
