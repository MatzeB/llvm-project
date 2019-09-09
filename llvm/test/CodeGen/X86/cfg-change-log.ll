; facebook T53546053
; RUN: llc < %s -O3 -cfg-change-log-funcs=test -o /dev/null 2>&1 | FileCheck %s

; CHECK: CFGChangeLog After Branch Probability Basic Block Placement (function: test)
; CHECK-NEXT: CFGChangeLog: changed %bb.{{[0-9]+}}.[[NAME1:[a-zA-Z\.]+]]
; CHECK-NEXT: -> %bb.{{[0-9]+}}.[[NAME1]]{{.+}}, outgoings 0x{{[0-9a-f]+}}:{{.+}} 0x{{[0-9a-f]+}}:{{.+}}
; CHECK-NEXT: CFGChangeLog: changed %bb.{{[0-9]+}}.[[NAME2:[a-zA-Z\.]+]]
; CHECK-NEXT: -> %bb.{{[0-9]+}}.[[NAME2]]{{.+}}, outgoings 0x{{[0-9a-f]+}}:{{.+}} 0x{{[0-9a-f]+}}:{{.+}}
; CHECK-NEXT: CFGChangeLog: deleted %bb.{{[0-9]+}}.loop

define i32 @test(i1 %c) {
entry:
  br label %loop

loop: ; preds = %BE2, %BE1, %0
  %iv = phi i32 [ 0, %entry ], [ %iv2, %be1 ], [ %iv2, %be2 ]
  store i32 %iv, i32* null
  %iv2 = add i32 %iv, 1
  br i1 %c, label %be1, label %be2, !prof !0

be1:  ; preds = %loop
  %p1 = icmp slt i32 %iv2, 300
  br i1 %p1, label %loop, label %exit, !prof !1

be2:    ; preds = %n br label %loop
  %p2 = icmp slt i32 %iv2, 300
  br i1 %p2, label %loop, label %exit, !prof !2

exit:
  %r = phi i32 [ %iv2, %be1 ], [ %iv2, %be2 ]
  ret i32 %r
}

!0 = !{!"branch_weights", i32 1, i32 299}
!1 = !{!"branch_weights", i32 1, i32 0}
!2 = !{!"branch_weights", i32 298, i32 1}
