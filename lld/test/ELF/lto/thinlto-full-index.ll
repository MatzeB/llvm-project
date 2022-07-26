;; facebook begin T124883009
; REQUIRES: x86
;; Test --thinlto-full-index for distributed ThinLTO.

; RUN: rm -rf %t && split-file %s %t && cd %t
; RUN: llvm-mc -filetype=obj -triple=x86_64 a.s -o a.o
; RUN: llvm-mc -filetype=obj -triple=x86_64 b.s -o b.o
; RUN: opt -module-summary c.ll -o c.bc
; RUN: opt -module-summary d.ll -o d.bc
; RUN: ld.lld --thinlto-index-only=thinlto.index --thinlto-full-index --thinlto-emit-imports-files --start-lib a.o --end-lib c.bc --start-lib b.o --end-lib d.bc -shared -o /dev/null
;; The index files and imports files are created.
; RUN: ls c.bc.thinlto.bc c.bc.imports d.bc.thinlto.bc d.bc.imports
;; Nothing to import.
; RUN: cat c.bc.imports | count 0
; RUN: cat d.bc.imports | count 0

;; Check that index file has 2 bitcode objects.
; RUN: FileCheck %s --check-prefix=CHECK-INDEX < thinlto.index
; CHECK-INDEX: c.bc
; CHECK-INDEX-NEXT: d.bc

;; Check that full index file has 2 bitcode objects plus 1 native object.
; RUN: FileCheck %s --check-prefix=CHECK-FULL < thinlto.index.full
; CHECK-FULL: c.bc
; CHECK-FULL-NEXT: b.o
; CHECK-FULL-NEXT: d.bc

;; No need for importing. Compile both bitcode objects to native objects.
; RUN: llc c.bc --filetype=obj -o c.o
; RUN: llc d.bc --filetype=obj -o d.o
;; Run final link following thinlto.index. Because there is no indication of
;; where to place the native objects, they are moved to the end of inputs.
; RUN: not ld.lld c.o d.o --start-lib a.o --end-lib --start-lib b.o --end-lib -o /dev/null 2>&1 | FileCheck %s --check-prefix=CHECK-DUP
; CHECK-DUP: duplicate symbol: g
; CHECK-DUP-NEXT: defined at a.o
; CHECK-DUP-NEXT: defined at b.o

;; Run final link following thinlto.index.full.
; RUN: ld.lld c.o b.o d.o -o /dev/null

;--- a.s
.globl g
g:
  ret

;--- b.s
.globl g
g:
  ret

.globl f
f:
  ret

;--- c.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @f()

define void @foo() {
  call void () @f()
  ret void
}

;--- d.ll
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @g()

define void @bar() {
  call void () @g()
  ret void
}
;; facebook end T124883009
