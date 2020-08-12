;; facebook T70955850
; RUN: opt < %s -enable-new-pm -passes='pseudo-probe,default<O3>' -S -o - | FileCheck %s

; First make sure the callsite is merged, then check whether its pseudo probe discriminator is generated correctly

declare dso_local void @foo() #0

define dso_local void @bar(i32 %0) #0 !dbg !11 {
  %2 = alloca i32, align 4
  store i32 %0, i32* %2, align 4
  call void @llvm.dbg.declare(metadata i32* %2, metadata !15, metadata !DIExpression()), !dbg !16
  %3 = load i32, i32* %2, align 4, !dbg !17
  %4 = icmp eq i32 %3, 0, !dbg !19
  br i1 %4, label %5, label %6, !dbg !20

5:
  call void @foo(), !dbg !21
  br label %7, !dbg !23

6:
  call void @foo(), !dbg !24
  br label %7

; CHECK: call void @foo(), !dbg ![[#DBG0:]]
; CHECK-NOT: call void @foo()

7:
  ret void, !dbg !26
}

declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

attributes #0 = { noinline nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone speculatable willreturn }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 12.0.0", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "test.c", directory: "any")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 12.0.0"}
!8 = !DISubroutineType(types: !9)
!9 = !{null}
!11 = distinct !DISubprogram(name: "bar", scope: !1, file: !1, line: 10, type: !12, scopeLine: 10, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !2)
!12 = !DISubroutineType(types: !13)
!13 = !{null, !14}
!14 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!15 = !DILocalVariable(name: "cond", arg: 1, scope: !11, file: !1, line: 10, type: !14)
!16 = !DILocation(line: 10, column: 14, scope: !11)
!17 = !DILocation(line: 11, column: 7, scope: !18)
!18 = distinct !DILexicalBlock(scope: !11, file: !1, line: 11, column: 7)
!19 = !DILocation(line: 11, column: 12, scope: !18)
!20 = !DILocation(line: 11, column: 7, scope: !11)
!21 = !DILocation(line: 12, column: 5, scope: !22)
!22 = distinct !DILexicalBlock(scope: !18, file: !1, line: 11, column: 18)
!23 = !DILocation(line: 13, column: 3, scope: !22)
!24 = !DILocation(line: 14, column: 5, scope: !25)
!25 = distinct !DILexicalBlock(scope: !18, file: !1, line: 13, column: 10)
!26 = !DILocation(line: 16, column: 1, scope: !11)


; CHECK: ![[#DBG0]] = !DILocation(line: [[#]], column: [[#]], scope: ![[#DBG1:]])
; CHECK: ![[#DBG1]] = !DILexicalBlockFile(scope: ![[#]], file: ![[#]], discriminator: 186646583)
