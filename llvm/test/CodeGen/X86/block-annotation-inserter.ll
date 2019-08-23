; facebook T62395623
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -persist-block-annotation -print-after=block-annotation-inserter -o /dev/null 2>&1 | FileCheck %s
; 
; CHECK-LABEL: foo
; CHECK-LABEL: bb.0.entry
; CHECK: INLINEASM{{.*}}__BLI.{{[0-9]+}}.0.10000:
; CHECK-LABEL: bb.2.if.end
; CHECK: INLINEASM{{.*}}__BLI.{{[0-9]+}}.2.10000:
; CHECK-LABEL: bb.1.if.then
; CHECK: INLINEASM{{.*}}__BLI.{{[0-9]+}}.1.1:
; CHECK-LABEL: foo_noprof
; CHECK-LABEL: bb.0.entry
; CHECK: INLINEASM{{.*}}__BLI.{{[0-9]+}}.0._:
; CHECK-LABEL: bb.2.if.end
; CHECK: INLINEASM{{.*}}__BLI.{{[0-9]+}}.2._:
; CHECK-LABEL: bb.1.if.then
; CHECK: INLINEASM{{.*}}__BLI.{{[0-9]+}}.1._:

; Function Attrs: uwtable
define i32 @foo(i32 %x) !prof !0 {
entry:
  %cmp = icmp slt i32 %x, 0
  br i1 %cmp, label %if.then, label %if.end, !prof !1

if.then:
  %add = add i32 %x, 1
  br label %if.end

if.end:
  %ret = phi i32 [ %x, %entry ], [ %add, %if.then ]
  ret i32 %ret
}

; Function Attrs: uwtable
define i32 @foo_noprof(i32 %x) {
entry:
  %cmp = icmp slt i32 %x, 0
  br i1 %cmp, label %if.then, label %if.end

if.then:
  %add = add i32 %x, 1
  br label %if.end

if.end:
  %ret = phi i32 [ %x, %entry ], [ %add, %if.then ]
  ret i32 %ret
}


!0 = !{!"function_entry_count", i32 10000}
!1 = !{!"branch_weights", i32 1, i32 10000}
