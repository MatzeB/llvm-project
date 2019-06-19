; facebook begin T46037538
; Check if block profile count is printed along with basic block name if -print-prof=names or -print-prof=all is provided.
; RUN: llc < %s -print-after=finalize-isel -print-prof=all -o /dev/null 2>&1 | FileCheck %s --check-prefix=BLOCKPROF 
; RUN: llc < %s -print-after=finalize-isel -print-prof=names -o /dev/null 2>&1 | FileCheck %s --check-prefix=BLOCKPROF 
; RUN: llc < %s -print-after=finalize-isel -print-for-dev -o /dev/null 2>&1 | FileCheck %s --check-prefix=BLOCKPROF 
; RUN: llc < %s -print-after=finalize-isel -print-prof=none -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-BLOCKPROF 
; RUN: llc < %s -print-after=finalize-isel -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-BLOCKPROF 

; Hexagon runs passes that renumber the basic blocks, causing this test
; to fail.
; XFAIL: hexagon

declare void @foo()

; Make sure we have the correct weight attached to each successor.
define i32 @test2(i32 %x) nounwind uwtable readnone ssp !prof !0 {
; BLOCKPROF-LABEL: Machine code for function test2:
; NO-BLOCKPROF-LABEL: Machine code for function test2:
entry:
  %conv = sext i32 %x to i64
  switch i64 %conv, label %return [
    i64 0, label %sw.bb
    i64 1, label %sw.bb
    i64 4, label %sw.bb
    i64 5, label %sw.bb1
    i64 15, label %sw.bb
  ], !prof !1
; BLOCKPROF: bb.0.entry (1000):
; BLOCKPROF: successors: %bb.1(0x75f8ebf2), %bb.4(0x0a07140e)
; NO-BLOCKPROF: bb.0.entry
; NO-BLOCKPROF-NOT: (1000)
; NO-BLOCKPROF: successors: %bb.1(0x75f8ebf2), %bb.4(0x0a07140e)
; BLOCKPROF: bb.4.entry (77):
; BLOCKPROF: successors: %bb.2(0x60606068), %bb.5(0x1f9f9f98)
; NO-BLOCKPROF: bb.4.entry
; NO-BLOCKPROF-NOT: (77)
; NO-BLOCKPROF: successors: %bb.2(0x60606068), %bb.5(0x1f9f9f98)
; BLOCKPROF: bb.5.entry (19):
; BLOCKPROF: successors: %bb.1(0x3cf3cf4b), %bb.6(0x430c30b5)
; NO-BLOCKPROF: bb.5.entry
; NO-BLOCKPROF-NOT:(19)
; NO-BLOCKPROF: successors: %bb.1(0x3cf3cf4b), %bb.6(0x430c30b5)
; BLOCKPROF: bb.6.entry (10):
; BLOCKPROF: successors: %bb.1(0x2e8ba2d7), %bb.3(0x51745d29)
; NO-BLOCKPROF: bb.6.entry
; NO-BLOCKPROF-NOT: (10)
; NO-BLOCKPROF: successors: %bb.1(0x2e8ba2d7), %bb.3(0x51745d29)

sw.bb:
; this call will prevent simplifyCFG from optimizing the block away in ARM/AArch64.
  tail call void @foo()
  br label %return

sw.bb1:
  br label %return

return:
  %retval.0 = phi i32 [ 5, %sw.bb1 ], [ 1, %sw.bb ], [ 0, %entry ]
  ret i32 %retval.0
}

!0 = !{!"function_entry_count", i64 1000}
!1 = !{!"branch_weights", i32 7, i32 6, i32 4, i32 4, i32 64, i21 1000}
