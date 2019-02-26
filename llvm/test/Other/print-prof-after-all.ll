; RUN: llvm-profdata merge %S/Inputs/print-prof-after-all.proftext -o %t.profdata
; RUN: opt < %s  -passes=pgo-instr-use -pgo-test-profile-file=%t.profdata -print-prof=all -S 2>&1 | FileCheck %s --check-prefix=ALL_INSTR
; RUN: opt < %s  -passes=pgo-instr-use -pgo-test-profile-file=%t.profdata -print-prof=names -S 2>&1 | FileCheck %s --check-prefix=NAMES_INSTR
; RUN: opt < %s  -passes=sample-profile -sample-profile-file=%S/Inputs/print-prof-after-all.prof -print-prof=all -S 2>&1 | FileCheck %s --check-prefix=ALL_SAMPLE
; RUN: opt < %s  -passes=sample-profile -sample-profile-file=%S/Inputs/print-prof-after-all.prof -print-prof=names -S 2>&1 | FileCheck %s --check-prefix=NAMES_SAMPLE

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"


@.str = private constant [9 x i8] c"Under 50\00", align 1

define i32 @main() #0 !dbg !7 {
entry:
  br label %for.body, !dbg !9
; ALL_INSTR: br label
; NAMES_INSTR-NOT: br label
; ALL_SAMPLE: br label
; NAMES_SAMPLE-NOT: br label

for.body:                                          
  %i = phi i32 [ 0, %entry ], [ %inc, %for.inc ]
  %cmp1 = icmp ult i32 %i, 50,  !dbg !11
  br i1 %cmp1, label %if.then, label %for.inc, !dbg !12
; ALL_INSTR: for.body:
; ALL_INSTR-NEXT: predecessors: %for.inc, %entry
; ALL_INSTR-NEXT: successors: %if.then(0x00000032), %for.body.for.inc_crit_edge(0x000003b6)
; NAMES_INSTR: for.body:
; NAMES_INSTR-NEXT: predecessors: %for.inc, %entry
; NAMES_INSTR-NEXT: successors: %if.then(0x00000032), %for.body.for.inc_crit_edge(0x000003b6)
; ALL_SAMPLE: for.body:
; ALL_SAMPLE-NEXT: predecessors: %for.inc, %entry
; ALL_SAMPLE-NEXT: successors: %if.then(0x00000003), %for.inc(0x00000027)
; NAMES_SAMPLE: for.body:
; NAMES_SAMPLE-NEXT: predecessors: %for.inc, %entry
; NAMES_SAMPLE-NEXT: successors: %if.then(0x00000003), %for.inc(0x00000027)

if.then:                  
  %call = tail call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([9 x i8], [9 x i8]* @.str, i64 0, i64 0)),  !dbg !13
  br label %for.inc,  !dbg !13

for.inc:                                        
  %inc = add nuw nsw i32 %i, 1,  !dbg !14
  %exitcond = icmp eq i32 %inc, 1000,  !dbg !15
  br i1 %exitcond, label %for.end, label %for.body, !dbg !9
; ALL_INSTR: for.inc:
; ALL_INSTR-NEXT: predecessors: %for.body.for.inc_crit_edge, %if.then
; ALL_INSTR-NEXT: successors: %for.end(0x00000001), %for.body(0x000003e7)
; NAMES_INSTR: for.inc:
; NAMES_INSTR-NEXT: predecessors: %for.body.for.inc_crit_edge, %if.then
; NAMES_INSTR-NEXT: successors: %for.end(0x00000001), %for.body(0x000003e7)
; ALL_SAMPLE: for.inc:
; ALL_SAMPLE-NEXT: predecessors: %if.then, %for.body
; ALL_SAMPLE-NEXT: successors: %for.end(0x00000002), %for.body(0x00000028)
; NAMES_SAMPLE: for.inc:
; NAMES_SAMPLE-NEXT: predecessors: %if.then, %for.body
; NAMES_SAMPLE-NEXT: successors: %for.end(0x00000002), %for.body(0x00000028)

for.end:                                
  ret i32 0, !dbg !10
}

declare i32 @printf(i8* nocapture readonly, ...)

attributes #0 = {"use-sample-profile"}

!llvm.module.flags = !{!3, !4, !5}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 7.0.0 (http://llvm.org/git/clang.git 7b0693d79162eca545845a8b2a82143ae955c1df) (http://llvm.org/git/llvm.git 4445572c3316ed16ed0181f5f87670699e3de940)", isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2)
!1 = !DIFile(filename: "main.c", directory: ".")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 7.0.0 (http://llvm.org/git/clang.git 7b0693d79162eca545845a8b2a82143ae955c1df) (http://llvm.org/git/llvm.git 4445572c3316ed16ed0181f5f87670699e3de940)"}
!7 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 2, type: !8, isLocal: false, isDefinition: true, scopeLine: 3, isOptimized: true, unit: !0, retainedNodes: !2)
!8 = !DISubroutineType(types: !2)
!9 = !DILocation(line: 4, column: 3, scope: !7)
!10 = !DILocation(line: 9, column: 3, scope: !7)
!11 = !DILocation(line: 6, column: 11, scope: !7)
!12 = !DILocation(line: 6, column: 9, scope: !7)
!13 = !DILocation(line: 7, column: 7, scope: !7)
!14 = !DILocation(line: 4, column: 30, scope: !7)
!15 = !DILocation(line: 4, column: 21, scope: !7)
