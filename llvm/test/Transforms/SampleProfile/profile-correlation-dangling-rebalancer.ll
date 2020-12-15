; facebook T68973288
; RUN: opt < %s -passes=pseudo-probe,sample-profile -sample-profile-use-mcf -sample-profile-mcf-rebalance-dangling -sample-profile-file=%S/Inputs/profile-correlation-dangling-rebalancer.prof | opt -analyze -branch-prob -enable-new-pm=0 | FileCheck %s

; Original C++ code for this test case:
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
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [32 x i8] c"countMultipliers(%d, %d) = %ld\0A\00", align 1

; Function Attrs: nounwind uwtable
define dso_local i32 @countMultipliers(i32 %0, i32 %1) #0 !dbg !21 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  %8 = alloca i32, align 4
  store i32 %0, i32* %3, align 4, !tbaa !35
  call void @llvm.dbg.declare(metadata i32* %3, metadata !26, metadata !DIExpression()), !dbg !39
  store i32 %1, i32* %4, align 4, !tbaa !35
  call void @llvm.dbg.declare(metadata i32* %4, metadata !27, metadata !DIExpression()), !dbg !40
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 1, i32 0, i64 -1), !dbg !41
  %9 = bitcast i32* %5 to i8*, !dbg !41
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %9) #5, !dbg !41
  call void @llvm.dbg.declare(metadata i32* %5, metadata !28, metadata !DIExpression()), !dbg !42
  store i32 0, i32* %5, align 4, !dbg !42, !tbaa !35
  %10 = bitcast i32* %6 to i8*, !dbg !43
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %10) #5, !dbg !43
  call void @llvm.dbg.declare(metadata i32* %6, metadata !29, metadata !DIExpression()), !dbg !44
  store i32 1, i32* %6, align 4, !dbg !44, !tbaa !35
  br label %11, !dbg !43

11:                                               ; preds = %40, %2
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 2, i32 0, i64 -1), !dbg !45
  %12 = load i32, i32* %6, align 4, !dbg !45, !tbaa !35
  %13 = load i32, i32* %3, align 4, !dbg !46, !tbaa !35
  %14 = icmp sle i32 %12, %13, !dbg !47
  br i1 %14, label %b17, label %15, !dbg !48

15:                                               ; preds = %11
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 3, i32 0, i64 -1), !dbg !49
  %16 = bitcast i32* %6 to i8*, !dbg !49
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %16) #5, !dbg !49
  br label %43

b17:                                               ; preds = %11
  %17 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 4, i32 0, i64 -1), !dbg !50
  %18 = bitcast i32* %7 to i8*, !dbg !50
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %18) #5, !dbg !50
  call void @llvm.dbg.declare(metadata i32* %7, metadata !31, metadata !DIExpression()), !dbg !51
  %19 = load i32, i32* %6, align 4, !dbg !52, !tbaa !35
  %20 = load i32, i32* %4, align 4, !dbg !54, !tbaa !35
  %21 = srem i32 %19, %20, !dbg !55
  %22 = icmp ne i32 %21, 0, !dbg !55
  br i1 %22, label %b23, label %b24, !dbg !56
; CHECK:  edge b17 -> b23 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b17 -> b24 probability is 0x40000000 / 0x80000000 = 50.00%

b23:                                               ; preds = %b17
  %23 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 5, i32 0, i64 -1), !dbg !57
  store i32 0, i32* %7, align 4, !dbg !57, !tbaa !35
  br label %b25, !dbg !59

b24:                                               ; preds = %b17
  %24 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 6, i32 0, i64 -1), !dbg !60
  store i32 1, i32* %7, align 4, !dbg !60, !tbaa !35
  br label %b25

b25:                                               ; preds = %b24, %b23
  %25 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 7, i32 0, i64 -1), !dbg !62
  %26 = bitcast i32* %8 to i8*, !dbg !62
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %26) #5, !dbg !62
  call void @llvm.dbg.declare(metadata i32* %8, metadata !34, metadata !DIExpression()), !dbg !63
  store i32 0, i32* %8, align 4, !dbg !63, !tbaa !35
  %27 = load i32, i32* %6, align 4, !dbg !64, !tbaa !35
  %28 = load i32, i32* %4, align 4, !dbg !66, !tbaa !35
  %29 = srem i32 %27, %28, !dbg !67
  %30 = icmp eq i32 %29, 0, !dbg !68
  br i1 %30, label %b31, label %b32, !dbg !69
; CHECK:  edge b25 -> b31 probability is 0x40000000 / 0x80000000 = 50.00%
; CHECK:  edge b25 -> b32 probability is 0x40000000 / 0x80000000 = 50.00%

b31:                                               ; preds = %b25
  %31 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 8, i32 0, i64 -1), !dbg !70
  store i32 1, i32* %8, align 4, !dbg !70, !tbaa !35
  br label %b32, !dbg !72

b32:                                               ; preds = %b31, %b25
  %32 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 9, i32 0, i64 -1), !dbg !73
  %33 = load i32, i32* %7, align 4, !dbg !73, !tbaa !35
  %34 = load i32, i32* %8, align 4, !dbg !74, !tbaa !35
  %35 = add nsw i32 %33, %34, !dbg !75
  %36 = load i32, i32* %5, align 4, !dbg !76, !tbaa !35
  %37 = add nsw i32 %36, %35, !dbg !76
  store i32 %37, i32* %5, align 4, !dbg !76, !tbaa !35
  %38 = bitcast i32* %8 to i8*, !dbg !77
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %38) #5, !dbg !77
  %39 = bitcast i32* %7 to i8*, !dbg !77
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %39) #5, !dbg !77
  br label %40, !dbg !78

40:                                               ; preds = %b32
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 10, i32 0, i64 -1), !dbg !79
  %41 = load i32, i32* %6, align 4, !dbg !79, !tbaa !35
  %42 = add nsw i32 %41, 1, !dbg !79
  store i32 %42, i32* %6, align 4, !dbg !79, !tbaa !35
  br label %11, !dbg !49, !llvm.loop !80

43:                                               ; preds = %15
  call void @llvm.pseudoprobe(i64 -5758218299531803684, i64 11, i32 0, i64 -1), !dbg !83
  %44 = load i32, i32* %5, align 4, !dbg !83, !tbaa !35
  %45 = sdiv i32 %44, 2, !dbg !84
  %46 = bitcast i32* %5 to i8*, !dbg !85
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %46) #5, !dbg !85
  ret i32 %45, !dbg !86
}

; Function Attrs: nofree nosync nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: argmemonly nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: inaccessiblememonly nounwind willreturn
declare void @llvm.pseudoprobe(i64, i64, i32, i64) #4

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" "use-sample-profile" }
attributes #1 = { nofree nosync nounwind readnone speculatable willreturn }
attributes #2 = { argmemonly nofree nosync nounwind willreturn }
attributes #3 = { "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #4 = { inaccessiblememonly nounwind willreturn }
attributes #5 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}
!llvm.pseudo_probe_desc = !{!7, !17}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 12.0.0 (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/llvm-project b8b59db389614bd58f0b2dd7841d48a6410e68b4)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "dangling.c", directory: "/home/vliaghat/logs/example")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 12.0.0 (ssh://git.vip.facebook.com/data/gitrepos/osmeta/external/llvm-project b8b59db389614bd58f0b2dd7841d48a6410e68b4)"}
!7 = !{i64 -5758218299531803684, i64 223598586707, !"countMultipliers", !8}
!8 = !{!9, !10, !11, !12, !13, !13, !14, !15, !16, !9, !2}
!9 = !{i32 2}
!10 = !{i32 4, i32 3}
!11 = !{i32 11}
!12 = !{i32 5, i32 6}
!13 = !{i32 7}
!14 = !{i32 8, i32 9}
!15 = !{i32 9}
!16 = !{i32 10}
!17 = !{i64 -2624081020897602054, i64 563057058432372, !"main", !18}
!18 = !{!9, !10, !19, !20, !9, !2}
!19 = !{i32 6}
!20 = !{i32 5}
!21 = distinct !DISubprogram(name: "countMultipliers", scope: !1, file: !1, line: 3, type: !22, scopeLine: 3, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !25)
!22 = !DISubroutineType(types: !23)
!23 = !{!24, !24, !24}
!24 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!25 = !{!26, !27, !28, !29, !31, !34}
!26 = !DILocalVariable(name: "n", arg: 1, scope: !21, file: !1, line: 3, type: !24)
!27 = !DILocalVariable(name: "k", arg: 2, scope: !21, file: !1, line: 3, type: !24)
!28 = !DILocalVariable(name: "mult", scope: !21, file: !1, line: 4, type: !24)
!29 = !DILocalVariable(name: "i", scope: !30, file: !1, line: 5, type: !24)
!30 = distinct !DILexicalBlock(scope: !21, file: !1, line: 5, column: 3)
!31 = !DILocalVariable(name: "a", scope: !32, file: !1, line: 6, type: !24)
!32 = distinct !DILexicalBlock(scope: !33, file: !1, line: 5, column: 32)
!33 = distinct !DILexicalBlock(scope: !30, file: !1, line: 5, column: 3)
!34 = !DILocalVariable(name: "b", scope: !32, file: !1, line: 13, type: !24)
!35 = !{!36, !36, i64 0}
!36 = !{!"int", !37, i64 0}
!37 = !{!"omnipotent char", !38, i64 0}
!38 = !{!"Simple C/C++ TBAA"}
!39 = !DILocation(line: 3, column: 26, scope: !21)
!40 = !DILocation(line: 3, column: 33, scope: !21)
!41 = !DILocation(line: 4, column: 3, scope: !21)
!42 = !DILocation(line: 4, column: 7, scope: !21)
!43 = !DILocation(line: 5, column: 8, scope: !30)
!44 = !DILocation(line: 5, column: 12, scope: !30)
!45 = !DILocation(line: 5, column: 19, scope: !33)
!46 = !DILocation(line: 5, column: 24, scope: !33)
!47 = !DILocation(line: 5, column: 21, scope: !33)
!48 = !DILocation(line: 5, column: 3, scope: !30)
!49 = !DILocation(line: 5, column: 3, scope: !33)
!50 = !DILocation(line: 6, column: 5, scope: !32)
!51 = !DILocation(line: 6, column: 9, scope: !32)
!52 = !DILocation(line: 7, column: 9, scope: !53)
!53 = distinct !DILexicalBlock(scope: !32, file: !1, line: 7, column: 9)
!54 = !DILocation(line: 7, column: 13, scope: !53)
!55 = !DILocation(line: 7, column: 11, scope: !53)
!56 = !DILocation(line: 7, column: 9, scope: !32)
!57 = !DILocation(line: 8, column: 9, scope: !58)
!58 = distinct !DILexicalBlock(scope: !53, file: !1, line: 7, column: 16)
!59 = !DILocation(line: 9, column: 5, scope: !58)
!60 = !DILocation(line: 10, column: 9, scope: !61)
!61 = distinct !DILexicalBlock(scope: !53, file: !1, line: 9, column: 12)
!62 = !DILocation(line: 13, column: 5, scope: !32)
!63 = !DILocation(line: 13, column: 9, scope: !32)
!64 = !DILocation(line: 14, column: 9, scope: !65)
!65 = distinct !DILexicalBlock(scope: !32, file: !1, line: 14, column: 9)
!66 = !DILocation(line: 14, column: 13, scope: !65)
!67 = !DILocation(line: 14, column: 11, scope: !65)
!68 = !DILocation(line: 14, column: 15, scope: !65)
!69 = !DILocation(line: 14, column: 9, scope: !32)
!70 = !DILocation(line: 15, column: 9, scope: !71)
!71 = distinct !DILexicalBlock(scope: !65, file: !1, line: 14, column: 21)
!72 = !DILocation(line: 16, column: 5, scope: !71)
!73 = !DILocation(line: 17, column: 13, scope: !32)
!74 = !DILocation(line: 17, column: 17, scope: !32)
!75 = !DILocation(line: 17, column: 15, scope: !32)
!76 = !DILocation(line: 17, column: 10, scope: !32)
!77 = !DILocation(line: 18, column: 3, scope: !33)
!78 = !DILocation(line: 18, column: 3, scope: !32)
!79 = !DILocation(line: 5, column: 28, scope: !33)
!80 = distinct !{!80, !48, !81, !82}
!81 = !DILocation(line: 18, column: 3, scope: !30)
!82 = !{!"llvm.loop.mustprogress"}
!83 = !DILocation(line: 19, column: 10, scope: !21)
!84 = !DILocation(line: 19, column: 15, scope: !21)
!85 = !DILocation(line: 20, column: 1, scope: !21)
!86 = !DILocation(line: 19, column: 3, scope: !21)
!87 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 22, type: !88, scopeLine: 22, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !90)
!88 = !DISubroutineType(types: !89)
!89 = !{!24}
!90 = !{!91, !92, !93, !95}
!91 = !DILocalVariable(name: "n", scope: !87, file: !1, line: 23, type: !24)
!92 = !DILocalVariable(name: "k", scope: !87, file: !1, line: 24, type: !24)
!93 = !DILocalVariable(name: "res", scope: !87, file: !1, line: 25, type: !94)
!94 = !DIBasicType(name: "long int", size: 64, encoding: DW_ATE_signed)
!95 = !DILocalVariable(name: "i", scope: !96, file: !1, line: 26, type: !24)
!96 = distinct !DILexicalBlock(scope: !87, file: !1, line: 26, column: 3)
!97 = !DILocation(line: 23, column: 3, scope: !87)
!98 = !DILocation(line: 23, column: 7, scope: !87)
!99 = !DILocation(line: 24, column: 3, scope: !87)
!100 = !DILocation(line: 24, column: 7, scope: !87)
!101 = !DILocation(line: 25, column: 3, scope: !87)
!102 = !DILocation(line: 25, column: 12, scope: !87)
!103 = !{!104, !104, i64 0}
!104 = !{!"long", !37, i64 0}
!105 = !DILocation(line: 26, column: 8, scope: !96)
!106 = !DILocation(line: 26, column: 12, scope: !96)
!107 = !DILocation(line: 26, column: 19, scope: !108)
!108 = distinct !DILexicalBlock(scope: !96, file: !1, line: 26, column: 3)
!109 = !DILocation(line: 26, column: 23, scope: !108)
!110 = !DILocation(line: 26, column: 21, scope: !108)
!111 = !DILocation(line: 26, column: 3, scope: !96)
!112 = !DILocation(line: 26, column: 3, scope: !108)
!113 = !DILocation(line: 27, column: 29, scope: !114)
!114 = distinct !DILexicalBlock(scope: !108, file: !1, line: 26, column: 31)
!115 = !DILocation(line: 27, column: 32, scope: !114)
!116 = !DILocation(line: 27, column: 12, scope: !117)
!117 = !DILexicalBlockFile(scope: !114, file: !1, discriminator: 186646591)
!118 = !DILocation(line: 27, column: 12, scope: !114)
!119 = !DILocation(line: 27, column: 9, scope: !114)
!120 = !DILocation(line: 28, column: 3, scope: !114)
!121 = !DILocation(line: 26, column: 27, scope: !108)
!122 = distinct !{!122, !111, !123, !82}
!123 = !DILocation(line: 28, column: 3, scope: !96)
!124 = !DILocation(line: 29, column: 10, scope: !87)
!125 = !DILocation(line: 29, column: 7, scope: !87)
!126 = !DILocation(line: 30, column: 46, scope: !87)
!127 = !DILocation(line: 30, column: 49, scope: !87)
!128 = !DILocation(line: 30, column: 52, scope: !87)
!129 = !DILocation(line: 30, column: 3, scope: !130)
!130 = !DILexicalBlockFile(scope: !87, file: !1, discriminator: 186646599)
!131 = !DILocation(line: 32, column: 1, scope: !87)
!132 = !DILocation(line: 31, column: 3, scope: !87)
