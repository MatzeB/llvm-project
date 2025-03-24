; REQUIRES: x86_64-linux
; REQUIRES: asserts
; RUN: opt < %s -passes=sample-profile -sample-profile-file=%S/Inputs/pseudo-probe-stale-profile-name-similarity.prof --salvage-stale-profile --salvage-unused-profile -S --debug-only=sample-profile,sample-profile-matcher,sample-profile-impl 2>&1 | FileCheck %s

; CHECK: Function _Z3fool is not in profile or profile symbol list.
; CHECK: Run stale profile matching for main
; CHECK: The functions _Z3fool(IR) and _Z3fooi(Profile) share the same base name: foo
; CHECK: Function:_Z3fool matches profile:_Z3fooi
; CHECK: Run stale profile matching for _Z3fool


target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@x = dso_local global i32 0, align 4

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local void @_Z3fool(i64 noundef %y) #0 !dbg !11 {
entry:
  %y.addr = alloca i64, align 8
  store i64 %y, ptr %y.addr, align 8
  call void @llvm.pseudoprobe(i64 5326982120444056491, i64 1, i32 0, i64 -1), !dbg !14
  %0 = load i64, ptr %y.addr, align 8, !dbg !14
  %1 = load volatile i32, ptr @x, align 4, !dbg !15
  %conv = sext i32 %1 to i64, !dbg !15
  %add = add nsw i64 %conv, %0, !dbg !15
  %conv1 = trunc i64 %add to i32, !dbg !15
  store volatile i32 %conv1, ptr @x, align 4, !dbg !15
  ret void, !dbg !16
}

; Function Attrs: mustprogress noinline norecurse nounwind optnone uwtable
define dso_local noundef i32 @main() #1 !dbg !17 {
entry:
  %retval = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  call void @llvm.pseudoprobe(i64 -2624081020897602054, i64 1, i32 0, i64 -1), !dbg !18
  store i32 0, ptr %i, align 4, !dbg !18
  br label %for.cond, !dbg !19

for.cond:                                         ; preds = %for.inc, %entry
  call void @llvm.pseudoprobe(i64 -2624081020897602054, i64 2, i32 0, i64 -1), !dbg !20
  %0 = load i32, ptr %i, align 4, !dbg !20
  %cmp = icmp slt i32 %0, 1000000, !dbg !21
  br i1 %cmp, label %for.body, label %for.end, !dbg !22

for.body:                                         ; preds = %for.cond
  call void @llvm.pseudoprobe(i64 -2624081020897602054, i64 3, i32 0, i64 -1), !dbg !23
  %1 = load i32, ptr %i, align 4, !dbg !23
  %conv = sext i32 %1 to i64, !dbg !23
  call void @_Z3fool(i64 noundef %conv), !dbg !24
  br label %for.inc, !dbg !26

for.inc:                                          ; preds = %for.body
  call void @llvm.pseudoprobe(i64 -2624081020897602054, i64 5, i32 0, i64 -1), !dbg !27
  %2 = load i32, ptr %i, align 4, !dbg !27
  %inc = add nsw i32 %2, 1, !dbg !27
  store i32 %inc, ptr %i, align 4, !dbg !27
  br label %for.cond, !dbg !22, !llvm.loop !28

for.end:                                          ; preds = %for.cond
  call void @llvm.pseudoprobe(i64 -2624081020897602054, i64 6, i32 0, i64 -1), !dbg !30
  %3 = load i32, ptr %retval, align 4, !dbg !30
  ret i32 %3, !dbg !30
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite)
declare void @llvm.pseudoprobe(i64, i64, i32, i64) #2

attributes #0 = { mustprogress noinline nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "use-sample-profile" }
attributes #1 = { mustprogress noinline norecurse nounwind optnone uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "use-sample-profile" }
attributes #2 = { nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite) }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6, !7}
!llvm.ident = !{!8}
!llvm.pseudo_probe_desc = !{!9, !10}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !1, producer: "clang version 17.0.4", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "test_rename.c", directory: "/home")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 1, !"wchar_size", i32 4}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"PIE Level", i32 2}
!6 = !{i32 7, !"uwtable", i32 2}
!7 = !{i32 7, !"frame-pointer", i32 2}
!8 = !{!"clang version 17.0.4"}
!9 = !{i64 5326982120444056491, i64 4294967295, !"_Z3fool"}
!10 = !{i64 -2624081020897602054, i64 281561395676419, !"main"}
!11 = distinct !DISubprogram(name: "foo", linkageName: "_Z3fool", scope: !1, file: !1, line: 3, type: !12, scopeLine: 3, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0)
!12 = !DISubroutineType(types: !13)
!13 = !{}
!14 = !DILocation(line: 4, column: 9, scope: !11)
!15 = !DILocation(line: 4, column: 6, scope: !11)
!16 = !DILocation(line: 5, column: 1, scope: !11)
!17 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 7, type: !12, scopeLine: 7, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !0)
!18 = !DILocation(line: 8, column: 12, scope: !17)
!19 = !DILocation(line: 8, column: 8, scope: !17)
!20 = !DILocation(line: 8, column: 19, scope: !17)
!21 = !DILocation(line: 8, column: 21, scope: !17)
!22 = !DILocation(line: 8, column: 3, scope: !17)
!23 = !DILocation(line: 9, column: 11, scope: !17)
!24 = !DILocation(line: 9, column: 7, scope: !25)
!25 = !DILexicalBlockFile(scope: !17, file: !1, discriminator: 455082023)
!26 = !DILocation(line: 10, column: 3, scope: !17)
!27 = !DILocation(line: 8, column: 37, scope: !17)
!28 = distinct !{!28, !22, !26, !29}
!29 = !{!"llvm.loop.mustprogress"}
!30 = !DILocation(line: 11, column: 1, scope: !17)