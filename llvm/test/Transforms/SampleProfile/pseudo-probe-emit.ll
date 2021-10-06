; REQUIRES: x86_64-linux
; RUN: opt < %s -passes=pseudo-probe -function-sections -S -o %t
; RUN: FileCheck %s < %t --check-prefix=CHECK-IL
; RUN: llc %t -stop-after=pseudo-probe-inserter -o - | FileCheck %s --check-prefix=CHECK-MIR
; RUN: llc %t -function-sections -filetype=asm -o %t1
; RUN: FileCheck %s < %t1 --check-prefix=CHECK-ASM
; RUN: llc %t -function-sections -filetype=obj -o %t2
; RUN: llvm-objdump --section-headers  %t2 | FileCheck %s --check-prefix=CHECK-OBJ
; RUN: llvm-mc %t1 -filetype=obj -o %t3
; RUN: llvm-objdump --section-headers  %t3 | FileCheck %s --check-prefix=CHECK-OBJ

; facebook begin T62536913
; RUN: opt < %s -passes=pseudo-probe -pseudo-probe-encode-cfg=1 -S -o %t4
; RUN: llc %t4 -filetype=asm -o - | FileCheck %s --check-prefix=CHECK-CFG
; facebook end T62536913

;; Check the generation of pseudoprobe intrinsic call.

@a = dso_local global i32 0, align 4

define void @foo(i32 %x) !dbg !3 {
bb0:
  %cmp = icmp eq i32 %x, 0
; CHECK-IL: call void @llvm.pseudoprobe(i64 [[#GUID:]], i64 1, i32 0, i64 -1), !dbg ![[#FAKELINE:]]
; CHECK-MIR: PSEUDO_PROBE [[#GUID:]], 1, 0, 0
; CHECK-ASM: .pseudoprobe	[[#GUID:]] 1 0 0
  br i1 %cmp, label %bb1, label %bb2

bb1:
; CHECK-IL: call void @llvm.pseudoprobe(i64 [[#GUID:]], i64 2, i32 0, i64 -1), !dbg ![[#FAKELINE]]
; CHECK-MIR: PSEUDO_PROBE [[#GUID]], 3, 0, 0
; CHECK-MIR: PSEUDO_PROBE [[#GUID]], 4, 0, 0
; CHECK-ASM: .pseudoprobe	[[#GUID]] 3 0 0
; CHECK-ASM: .pseudoprobe	[[#GUID]] 4 0 0
  store i32 6, ptr @a, align 4
  br label %bb3

bb2:
; CHECK-IL: call void @llvm.pseudoprobe(i64 [[#GUID:]], i64 3, i32 0, i64 -1), !dbg ![[#FAKELINE]]
; CHECK-MIR: PSEUDO_PROBE [[#GUID]], 2, 0, 0
; CHECK-MIR: PSEUDO_PROBE [[#GUID]], 4, 0, 0
; CHECK-ASM: .pseudoprobe	[[#GUID]] 2 0 0
; CHECK-ASM: .pseudoprobe	[[#GUID]] 4 0 0
  store i32 8, ptr @a, align 4
  br label %bb3

bb3:
; CHECK-IL: call void @llvm.pseudoprobe(i64 [[#GUID]], i64 4, i32 0, i64 -1), !dbg ![[#REALLINE:]]
  ret void, !dbg !12
}

declare void @bar(i32 %x) 

define internal void @foo2(ptr %f) !dbg !4 {
entry:
; CHECK-IL: call void @llvm.pseudoprobe(i64 [[#GUID2:]], i64 1, i32 0, i64 -1)
; CHECK-MIR: PSEUDO_PROBE [[#GUID2:]], 1, 0, 0
; CHECK-ASM: .pseudoprobe	[[#GUID2:]] 1 0 0
; Check pseudo_probe metadata attached to the indirect call instruction.
; CHECK-IL: call void %f(i32 1), !dbg ![[#PROBE0:]]
; CHECK-MIR: PSEUDO_PROBE [[#GUID2]], 2, 1, 0
; CHECK-ASM: .pseudoprobe	[[#GUID2]] 2 1 0
  call void %f(i32 1), !dbg !13
; Check pseudo_probe metadata attached to the direct call instruction.
; CHECK-IL: call void @bar(i32 1), !dbg ![[#PROBE1:]]
; CHECK-MIR: PSEUDO_PROBE	[[#GUID2]], 3, 2, 0
; CHECK-ASM: .pseudoprobe	[[#GUID2]] 3 2 0
  call void @bar(i32 1)
  ret void
}

; CHECK-IL: Function Attrs: inaccessiblememonly nocallback nofree nosync nounwind willreturn
; CHECK-IL-NEXT: declare void @llvm.pseudoprobe(i64, i64, i32, i64)

; CHECK-IL: ![[#FOO:]] = distinct !DISubprogram(name: "foo"
; CHECK-IL: ![[#FAKELINE]] = !DILocation(line: 0, scope: ![[#FOO]])
; CHECK-IL: ![[#REALLINE]] = !DILocation(line: 2, scope: ![[#FOO]])
; CHECK-IL: ![[#PROBE0]] = !DILocation(line: 2, column: 20, scope: ![[#SCOPE0:]])
;; facebook T96694365
;; A discriminator of 513410456304484352 which is 0x720001700000000 in hexdecimal, stands for a direct call probe
;; with an index of 2.
; CHECK-IL: ![[#SCOPE0]] = !DILexicalBlockFile(scope: ![[#]], file: ![[#]], discriminator: 513410456304484352)
; CHECK-IL: ![[#PROBE1]] = !DILocation(line: 0, scope: ![[#SCOPE1:]])
;; facebook T96694365
;; A discriminator of 801640866815934464 which is 0xb20001f00000000 in hexdecimal, stands for a direct call probe
;; with an index of 3.
; CHECK-IL: ![[#SCOPE1]] = !DILexicalBlockFile(scope: ![[#]], file: ![[#]], discriminator: 801640866815934464)

; Check the generation of .pseudo_probe_desc section
; CHECK-ASM: .section .pseudo_probe_desc,"G",@progbits,.pseudo_probe_desc_foo,comdat
; CHECK-ASM-NEXT: .quad [[#GUID]]
; CHECK-ASM-NEXT: .quad [[#HASH:]]
; CHECK-ASM-NEXT: .byte  3
; CHECK-ASM-NEXT: .ascii	"foo"
; CHECK-ASM-NEXT: .section  .pseudo_probe_desc,"G",@progbits,.pseudo_probe_desc_foo2,comdat
; CHECK-ASM-NEXT: .quad [[#GUID2]]
; CHECK-ASM-NEXT: .quad [[#HASH2:]]
; CHECK-ASM-NEXT: .byte 4
; CHECK-ASM-NEXT: .ascii	"foo2"

; CHECK-OBJ-COUNT-2: .pseudo_probe_desc
; CHECK-OBJ-COUNT-2: .pseudo_probe

; facebook begin T62536913
; Check the generation of .pseudo_probe_desc section with CFG encoding.
; CHECK-CFG: .section	.pseudo_probe_desc,"",@progbits
; CHECK-CFG-NEXT: .quad [[#GUID:]]
; CHECK-CFG-NEXT: .quad [[#HASH:]]
; CHECK-CFG-NEXT: .byte  3
; CHECK-CFG-NEXT: .ascii	"foo"
; CHECK-CFG-NEXT: .byte 4
; CHECK-CFG-NEXT: .byte 2
; CHECK-CFG-NEXT: .byte 2
; CHECK-CFG-NEXT: .byte 3
; CHECK-CFG-NEXT: .byte 1
; CHECK-CFG-NEXT: .byte 4
; CHECK-CFG-NEXT: .byte 1
; CHECK-CFG-NEXT: .byte 4
; CHECK-CFG-NEXT: .byte 0
; CHECK-CFG-NEXT: .quad [[#GUID2:]]
; CHECK-CFG-NEXT: .quad [[#HASH2:]]
; CHECK-CFG-NEXT: .byte 4
; CHECK-CFG-NEXT: .ascii	"foo2"
; CHECK-CFG-NEXT: .byte 1
; CHECK-CFG-NEXT: .byte 0
; facebook end T62536913

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!9, !10}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1)
!1 = !DIFile(filename: "test.c", directory: "")
!2 = !{}
!3 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 1, type: !5, unit: !0, retainedNodes: !2)
!4 = distinct !DISubprogram(name: "foo2", scope: !1, file: !1, line: 2, type: !5, unit: !0, retainedNodes: !2)
!5 = !DISubroutineType(types: !6)
!6 = !{!7}
!7 = !DIBasicType(name: "int", size: 32, align: 32, encoding: DW_ATE_signed)
!9 = !{i32 2, !"Dwarf Version", i32 4}
!10 = !{i32 2, !"Debug Info Version", i32 3}
!11 = !{!"clang version 3.9.0"}
!12 = !DILocation(line: 2, scope: !3)
!13 = !DILocation(line: 2, column: 20, scope: !4)
