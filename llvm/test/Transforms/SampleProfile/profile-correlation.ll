; facebook T68973288
; RUN: opt < %s -passes=pseudo-probe,sample-profile -sample-profile-use-mcf -sample-profile-file=%S/Inputs/profile-correlation.prof | opt -analyze -branch-prob -enable-new-pm=0 | FileCheck %s

; Original C++ code for this test case:
;
; #include <stdio.h>
;
; int sum_of_squares(int n, int k) {
;   int res = 0;
;   for (int i = 0; i <= n; i++) {
;     if (i % k) {
;       res += i * i;
;     } else {
;       res += i;
;     }
;   }
;   return res;
; }
;
; int main() {
;   int n = 9999991;
;   int k = 3;
;   printf("sum_of_squares(%d, %d) = %d\n", n, k, sum_of_squares(n, k));
;   return 0;
; }

; ModuleID = 'squares.c'
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [29 x i8] c"sum_of_squares(%d, %d) = %d\0A\00", align 1

; Function Attrs: nounwind uwtable
define dso_local i32 @sum_of_squares(i32 %0, i32 %1) #0 !dbg !17 {
b2:
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %6 = alloca i32, align 4
  store i32 %0, i32* %3, align 4, !tbaa !27
  call void @llvm.dbg.declare(metadata i32* %3, metadata !22, metadata !DIExpression()), !dbg !31
  store i32 %1, i32* %4, align 4, !tbaa !27
  call void @llvm.dbg.declare(metadata i32* %4, metadata !23, metadata !DIExpression()), !dbg !32
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 1, i32 0, i64 -1), !dbg !33
  %7 = bitcast i32* %5 to i8*, !dbg !33
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %7) #5, !dbg !33
  call void @llvm.dbg.declare(metadata i32* %5, metadata !24, metadata !DIExpression()), !dbg !34
  store i32 0, i32* %5, align 4, !dbg !34, !tbaa !27
  %8 = bitcast i32* %6 to i8*, !dbg !35
  call void @llvm.lifetime.start.p0i8(i64 4, i8* %8) #5, !dbg !35
  call void @llvm.dbg.declare(metadata i32* %6, metadata !25, metadata !DIExpression()), !dbg !36
  store i32 0, i32* %6, align 4, !dbg !36, !tbaa !27
  br label %b9, !dbg !35
; CHECK:  edge b2 -> b9 probability is 0x80000000 / 0x80000000 = 100.00%

b9:                                                ; preds = %b31, %b2
  %9 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 2, i32 0, i64 -1), !dbg !37
  %10 = load i32, i32* %6, align 4, !dbg !37, !tbaa !27
  %11 = load i32, i32* %3, align 4, !dbg !39, !tbaa !27
  %12 = icmp sle i32 %10, %11, !dbg !40
  br i1 %12, label %b15, label %b13, !dbg !41
; CHECK:  edge b9 -> b15 probability is 0x7ff51177 / 0x80000000 = 99.97%
; CHECK:  edge b9 -> b13 probability is 0x000aee89 / 0x80000000 = 0.03%

b13:                                               ; preds = %b9
  %13 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 3, i32 0, i64 -1), !dbg !42
  %14 = bitcast i32* %6 to i8*, !dbg !42
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %14) #5, !dbg !42
  br label %b34
; CHECK:  edge b13 -> b34 probability is 0x80000000 / 0x80000000 = 100.00%

b15:                                               ; preds = %b9
  %15 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 4, i32 0, i64 -1), !dbg !43
  %16 = load i32, i32* %6, align 4, !dbg !43, !tbaa !27
  %17 = load i32, i32* %4, align 4, !dbg !46, !tbaa !27
  %18 = srem i32 %16, %17, !dbg !47
  %19 = icmp ne i32 %18, 0, !dbg !47
  br i1 %19, label %b20, label %b26, !dbg !48
; CHECK:  edge b15 -> b20 probability is 0x7ffa8880 / 0x80000000 = 99.98%
; CHECK:  edge b15 -> b26 probability is 0x00057780 / 0x80000000 = 0.02%

b20:                                               ; preds = %b15
  %20 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 5, i32 0, i64 -1), !dbg !49
  %21 = load i32, i32* %6, align 4, !dbg !49, !tbaa !27
  %22 = load i32, i32* %6, align 4, !dbg !51, !tbaa !27
  %23 = mul nsw i32 %21, %22, !dbg !52
  %24 = load i32, i32* %5, align 4, !dbg !53, !tbaa !27
  %25 = add nsw i32 %24, %23, !dbg !53
  store i32 %25, i32* %5, align 4, !dbg !53, !tbaa !27
  br label %b30, !dbg !54
; CHECK:  edge b20 -> b30 probability is 0x80000000 / 0x80000000 = 100.00%

b26:                                               ; preds = %b15
  %26 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 6, i32 0, i64 -1), !dbg !55
  %27 = load i32, i32* %6, align 4, !dbg !55, !tbaa !27
  %28 = load i32, i32* %5, align 4, !dbg !57, !tbaa !27
  %29 = add nsw i32 %28, %27, !dbg !57
  store i32 %29, i32* %5, align 4, !dbg !57, !tbaa !27
  br label %b30
; CHECK:  edge b26 -> b30 probability is 0x80000000 / 0x80000000 = 100.00%

b30:                                               ; preds = %b26, %b20
  %30 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 7, i32 0, i64 -1), !dbg !58
  br label %b31, !dbg !58
; CHECK:  edge b30 -> b31 probability is 0x80000000 / 0x80000000 = 100.00%

b31:                                               ; preds = %b30
  %31 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 8, i32 0, i64 -1), !dbg !59
  %32 = load i32, i32* %6, align 4, !dbg !59, !tbaa !27
  %33 = add nsw i32 %32, 1, !dbg !59
  store i32 %33, i32* %6, align 4, !dbg !59, !tbaa !27
  br label %b9, !dbg !42, !llvm.loop !60
; CHECK:  edge b31 -> b9 probability is 0x80000000 / 0x80000000 = 100.00%

b34:                                               ; preds = %b13
  %34 = alloca i32, align 4
  call void @llvm.pseudoprobe(i64 -907520326213521421, i64 9, i32 0, i64 -1), !dbg !62
  %35 = load i32, i32* %5, align 4, !dbg !62, !tbaa !27
  %36 = bitcast i32* %5 to i8*, !dbg !63
  call void @llvm.lifetime.end.p0i8(i64 4, i8* %36) #5, !dbg !63
  ret i32 %35, !dbg !64
}

; Function Attrs: nounwind readnone speculatable willreturn
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.lifetime.start.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: argmemonly nounwind willreturn
declare void @llvm.lifetime.end.p0i8(i64 immarg, i8* nocapture) #2

; Function Attrs: inaccessiblememonly nounwind willreturn
declare void @llvm.pseudoprobe(i64, i64, i32, i64) #4

attributes #0 = { nounwind uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="none" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" "use-sample-profile"}
attributes #1 = { nounwind readnone speculatable willreturn }
attributes #2 = { argmemonly nounwind willreturn }
attributes #4 = { inaccessiblememonly nounwind willreturn }
attributes #5 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}
!llvm.ident = !{!6}
!llvm.pseudo_probe_desc = !{!7, !15}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 12.0.0 (trunk)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "squares.c", directory: ".")
!2 = !{}
!3 = !{i32 7, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{!"clang version 12.0.0 (trunk)"}
!7 = !{i64 -907520326213521421, i64 175862120757, !"sum_of_squares", !8}
!8 = !{!9, !10, !11, !12, !13, !13, !14, !9, !2}
!9 = !{i32 2}
!10 = !{i32 4, i32 3}
!11 = !{i32 9}
!12 = !{i32 5, i32 6}
!13 = !{i32 7}
!14 = !{i32 8}
!15 = !{i64 -2624081020897602054, i64 4294967295, !"main", !16}
!16 = !{!2}
!17 = distinct !DISubprogram(name: "sum_of_squares", scope: !1, file: !1, line: 3, type: !18, scopeLine: 3, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !21)
!18 = !DISubroutineType(types: !19)
!19 = !{!20, !20, !20}
!20 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!21 = !{!22, !23, !24, !25}
!22 = !DILocalVariable(name: "n", arg: 1, scope: !17, file: !1, line: 3, type: !20)
!23 = !DILocalVariable(name: "k", arg: 2, scope: !17, file: !1, line: 3, type: !20)
!24 = !DILocalVariable(name: "res", scope: !17, file: !1, line: 4, type: !20)
!25 = !DILocalVariable(name: "i", scope: !26, file: !1, line: 5, type: !20)
!26 = distinct !DILexicalBlock(scope: !17, file: !1, line: 5, column: 3)
!27 = !{!28, !28, i64 0}
!28 = !{!"int", !29, i64 0}
!29 = !{!"omnipotent char", !30, i64 0}
!30 = !{!"Simple C/C++ TBAA"}
!31 = !DILocation(line: 3, column: 24, scope: !17)
!32 = !DILocation(line: 3, column: 31, scope: !17)
!33 = !DILocation(line: 4, column: 3, scope: !17)
!34 = !DILocation(line: 4, column: 7, scope: !17)
!35 = !DILocation(line: 5, column: 8, scope: !26)
!36 = !DILocation(line: 5, column: 12, scope: !26)
!37 = !DILocation(line: 5, column: 19, scope: !38)
!38 = distinct !DILexicalBlock(scope: !26, file: !1, line: 5, column: 3)
!39 = !DILocation(line: 5, column: 24, scope: !38)
!40 = !DILocation(line: 5, column: 21, scope: !38)
!41 = !DILocation(line: 5, column: 3, scope: !26)
!42 = !DILocation(line: 5, column: 3, scope: !38)
!43 = !DILocation(line: 6, column: 9, scope: !44)
!44 = distinct !DILexicalBlock(scope: !45, file: !1, line: 6, column: 9)
!45 = distinct !DILexicalBlock(scope: !38, file: !1, line: 5, column: 32)
!46 = !DILocation(line: 6, column: 13, scope: !44)
!47 = !DILocation(line: 6, column: 11, scope: !44)
!48 = !DILocation(line: 6, column: 9, scope: !45)
!49 = !DILocation(line: 7, column: 14, scope: !50)
!50 = distinct !DILexicalBlock(scope: !44, file: !1, line: 6, column: 16)
!51 = !DILocation(line: 7, column: 18, scope: !50)
!52 = !DILocation(line: 7, column: 16, scope: !50)
!53 = !DILocation(line: 7, column: 11, scope: !50)
!54 = !DILocation(line: 8, column: 5, scope: !50)
!55 = !DILocation(line: 9, column: 14, scope: !56)
!56 = distinct !DILexicalBlock(scope: !44, file: !1, line: 8, column: 12)
!57 = !DILocation(line: 9, column: 11, scope: !56)
!58 = !DILocation(line: 11, column: 3, scope: !45)
!59 = !DILocation(line: 5, column: 28, scope: !38)
!60 = distinct !{!60, !41, !61}
!61 = !DILocation(line: 11, column: 3, scope: !26)
!62 = !DILocation(line: 12, column: 10, scope: !17)
!63 = !DILocation(line: 13, column: 1, scope: !17)
!64 = !DILocation(line: 12, column: 3, scope: !17)
!65 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 15, type: !66, scopeLine: 15, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !68)
!66 = !DISubroutineType(types: !67)
!67 = !{!20}
!68 = !{!69, !70}
!69 = !DILocalVariable(name: "n", scope: !65, file: !1, line: 16, type: !20)
!70 = !DILocalVariable(name: "k", scope: !65, file: !1, line: 17, type: !20)
!71 = !DILocation(line: 16, column: 3, scope: !65)
!72 = !DILocation(line: 16, column: 7, scope: !65)
!73 = !DILocation(line: 17, column: 3, scope: !65)
!74 = !DILocation(line: 17, column: 7, scope: !65)
!75 = !DILocation(line: 18, column: 43, scope: !65)
!76 = !DILocation(line: 18, column: 46, scope: !65)
!77 = !DILocation(line: 18, column: 64, scope: !65)
!78 = !DILocation(line: 18, column: 67, scope: !65)
!79 = !DILocation(line: 18, column: 49, scope: !80)
!80 = !DILexicalBlockFile(scope: !65, file: !1, discriminator: 33554434)
!81 = !DILocation(line: 18, column: 3, scope: !82)
!82 = !DILexicalBlockFile(scope: !65, file: !1, discriminator: 33554435)
!83 = !DILocation(line: 20, column: 1, scope: !65)
!84 = !DILocation(line: 19, column: 3, scope: !65)
