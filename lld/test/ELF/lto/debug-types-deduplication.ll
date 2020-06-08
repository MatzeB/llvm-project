; facebook T66645141
; REQUIRES: x86
; RUN: llvm-as %s -o %t.o
; RUN: llvm-as %p/Inputs/debug-types-deduplication.ll -o %t2.o
; RUN: ld.lld %t.o %t2.o -mllvm --generate-type-units --entry=main -o %t3.o
; RUN: llvm-dwarfdump --debug-types %t3.o | FileCheck %s

; CHECK-COUNT-1: Type Unit:
; CHECK-NOT: Type Unit:

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.foo = type { i32 }

define dso_local i32 @main() noinline optnone !dbg !4 {
  %1 = alloca %struct.foo, align 4
  call void @llvm.dbg.declare(metadata %struct.foo* %1, metadata !7, metadata !DIExpression()), !dbg !11
  ret i32 0, !dbg !12
}

declare void @llvm.dbg.declare(metadata, metadata, metadata)

declare dso_local i32 @_Z3bar3foo(i32)

declare void @llvm.memcpy.p0i8.p0i8.i64(i8* noalias nocapture writeonly, i8* noalias nocapture readonly, i64, i1 immarg)


!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "hello.cc", directory: "")
!2 = !{i32 7, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "main", type: !5, unit: !0)
!5 = !DISubroutineType(types: !{!6})
!6 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!7 = !DILocalVariable(name: "f1", scope: !4, file: !1,type: !8)
!8 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "foo", file: !9, line: 4, size: 32, flags: DIFlagTypePassByValue, elements: !{!10}, identifier: "_ZTS3foo")
!9 = !DIFile(filename: "./foo.h", directory: "")
!10 = !DIDerivedType(tag: DW_TAG_member, name: "f", scope: !8, file: !9, line: 4, baseType: !6, size: 32)
!11 = !DILocation(line: 1, column: 1, scope: !4)
!12 = !DILocation(line: 2, column: 2, scope: !4)

^0 = module: (path: "hello.o", hash: (339834615, 71091658, 394989773, 1362321694, 1785275294))
