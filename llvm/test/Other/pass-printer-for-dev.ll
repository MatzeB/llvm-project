; facebook T44360418
; RUN: opt < %s -disable-output -passes=no-op-module -print-before-all \
; RUN:   -print-for-dev 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not="call void @llvm.dbg" \
; RUN:   --implicit-check-not="DILocation"
target triple = "x86_64--"

; CHECK-LABEL: IR Dump Before NoOpModulePass
; CHECK: Function Profile: function_entry_count(101)
; CHECK: define i32 @foo
; CHECK-SAME: [ tiny.c:1 ]
; CHECK-NOT: !dbg
; CHECK: [ tiny.c:2:3 ]
; CHECK: [ tiny.c:2:10 ]
;
; CHECK: Function Profile: function_entry_count(~0)
; CHECK: define void @bar()

define i32 @foo(i32 %a0) !dbg !3 !prof !8 {
  %1 = alloca i32, align 4
  store i32 %a0, ptr %1, align 4
  call void @llvm.dbg.declare(metadata ptr %1, metadata !9, metadata !DIExpression()), !dbg !10
  br label %2, !dbg !11

2:
  %3 = load i32, ptr %1, align 4
  %4 = icmp sgt i32 %3, 15
  br i1 %4, label %5, label %8

5:
  %6 = load i32, ptr %1, align 4
  %7 = shl i32 %6, 1
  store i32 %7, ptr %1, align 4
  br label %2

8:
  %9 = load i32, ptr %1, align 4
  ret i32 %9, !dbg !12
}

define void @bar() #0 !prof !13 {
  ret void
}

declare void @llvm.dbg.declare(metadata, metadata, metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, nameTableKind: None)
!1 = !DIFile(filename: "tiny.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 1, type: !4, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !7)
!4 = !DISubroutineType(types: !5)
!5 = !{!6, !6}
!6 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!7 = !{}
!8 = !{!"function_entry_count", i64 101}
!9 = !DILocalVariable(name: "x", arg: 1, scope: !3, file: !1, line: 1, type: !6)
!10 = !DILocation(line: 1, column: 13, scope: !3)
!11 = !DILocation(line: 2, column: 3, scope: !3)
!12 = !DILocation(line: 2, column: 10, scope: !3)
!13 = !{!"function_entry_count", i64 18446744073709551615}
