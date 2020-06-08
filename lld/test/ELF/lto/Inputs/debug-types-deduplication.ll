; facebook T66645141
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.foo = type { i32 }

define dso_local i32 @_Z3bar3foo(i32 %0) noinline optnone !dbg !4 {
  %2 = alloca %struct.foo, align 4
  call void @llvm.dbg.declare(metadata %struct.foo* %2, metadata !10, metadata !DIExpression()), !dbg !11
  ret i32 0, !dbg !12
}

declare void @llvm.dbg.declare(metadata, metadata, metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "foo.cc", directory: "")
!2 = !{i32 7, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "bar", linkageName: "_Z3bar3foo", type: !5, unit: !0)
!5 = !DISubroutineType(types: !{!6, !7})
!6 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!7 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "foo", file: !8, line: 4, size: 32, flags: DIFlagTypePassByValue, elements: !{!9}, identifier: "_ZTS3foo")
!8 = !DIFile(filename: "./foo.h", directory: "")
!9 = !DIDerivedType(tag: DW_TAG_member, name: "f", scope: !7, file: !8, line: 4, baseType: !6, size: 32)
!10 = !DILocalVariable(name: "f1", arg: 1, scope: !4, file: !1, line: 3, type: !7)
!11 = !DILocation(line: 1, column: 1, scope: !4)
!12 = !DILocation(line: 2, column: 1, scope: !4)

^0 = module: (path: "foo.o", hash: (505897185, 3065645384, 2109903963, 2773340897, 2672438121))
^1 = gv: (name: "_Z3bar3foo", summaries: (function: (module: ^0, flags: (linkage: external, notEligibleToImport: 0, live: 0, dsoLocal: 1, canAutoHide: 0), insts: 6, funcFlags: (readNone: 0, readOnly: 0, noRecurse: 0, returnDoesNotAlias: 0, noInline: 1, alwaysInline: 0)))) ; guid = 11911810691311191905
