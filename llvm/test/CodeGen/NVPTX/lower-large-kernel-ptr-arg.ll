; facebook begin T86926906
; UNSUPPORTED: true
; facebook end T86926906
; facebook begin T64625594
; RUN: opt < %s -nvptx-lower-args -S | FileCheck %s
; RUN: opt < %s -nvptx-lower-args -nvptx-max-arg-size-to-lower=100 -S | FileCheck %s --check-prefix=LIMIT-ARG

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "nvptx64-unknown-unknown"

%struct.Large = type { [100 x i8] }
%struct.Small = type { [50 x i8] }

; Read from Large struct
define void @ReadFromLargeStruct(%struct.Large* byval(%struct.Large) %s, i8* %out) #0 {
entry:
; CHECK-LABEL: @ReadFromLargeStruct
; MAX-ARG-LABEL: @ReadFromLargeStruct
; CHECK: [[TMP1:%.*]] = alloca %struct.Large
; CHECK: [[TMP2:%.*]] = addrspacecast %struct.Large* %s to %struct.Large addrspace(101)* 
; CHECK: [[TMP3:%.*]] = load %struct.Large, %struct.Large addrspace(101)* [[TMP2]]
; CHECK: store %struct.Large [[TMP3]], %struct.Large* [[TMP1]]
; LIMIT-ARG-NOT: alloca %struct.Large
; LIMIT-ARG: addrspacecast %struct.Large* %s to %struct.Large addrspace(101)*
  %v = getelementptr inbounds %struct.Large, %struct.Large* %s, i32 0, i32 0, i64 42
  %0 = load i8, i8* %v, align 2
  store i8 %0, i8* %out, align 1
  ret void
}

; Read from Small struct
define void @ReadFromSmallStruct(%struct.Small* byval(%struct.Small) %s, i8* %out) #0 {
; CHECK-LABEL: @ReadFromSmallStruct
; MAX-ARG-LABEL: @ReadFromSmallStruct
; CHECK: [[TMP1:%.*]] = alloca %struct.Small
; CHECK: [[TMP2:%.*]] = addrspacecast %struct.Small* %s to %struct.Small addrspace(101)*
; CHECK: [[TMP3:%.*]] = load %struct.Small, %struct.Small addrspace(101)* [[TMP2:%.*]]
; CHECK: store %struct.Small %s3, %struct.Small*
; LIMIT-ARG: alloca %struct.Small
; LIMIT-ARG: addrspacecast %struct.Small* %s to %struct.Small addrspace(101)*
; LIMIT-ARG: load %struct.Small, %struct.Small addrspace(101)*
; LIMIT-ARG: store %struct.Small %s3, %struct.Small*
  %v = getelementptr inbounds %struct.Small, %struct.Small* %s, i32 0, i32 0, i64 42
  %1 = load i8, i8* %v, align 2
  store i8 %1, i8* %out, align 1
  ret void
}

; Store into Large struct
define void @StoreInst(%struct.Large* byval(%struct.Large) %s) #0 {
; CHECK-LABEL: @StoreInst
; MAX-ARG-LABEL: @StoreInst
; CHECK: alloca %struct.Large
; MAX-ARG-LABEL: alloca %struct.Large
  %v = getelementptr inbounds %struct.Large, %struct.Large* %s, i32 0, i32 0, i64 42
  store i8 12, i8* %v, align 2
  ret void
}

; Select with Large struct
define void @SelectInst(%struct.Large* byval(%struct.Large) %s, i1 %p, i8* %out) #0 {
; CHECK-LABEL: @SelectInst
; MAX-ARG-LABEL: @SelectInst
; CHECK: alloca %struct.Large
; MAX-ARG-LABEL: alloca %struct.Large
  %v1 = getelementptr inbounds %struct.Large, %struct.Large* %s, i64 0, i32 0, i64 42
  %v2 = alloca i8, align 1
  store i8 42, i8* %v2, align 1
  %a = select i1 %p, i8* %v1, i8* %v2
  %1 = load i8, i8* %a, align 2
  store i8 %1, i8* %out, align 1
  ret void
}

; Phi with Large struct
define void @PhiNode(%struct.Large* byval(%struct.Large) %s, i32 %n, i8* %out) #0 {
; CHECK-LABEL: @PhiNode
; MAX-ARG-LABEL: @PhiNode
; CHECK: alloca %struct.Large
; MAX-ARG-LABEL: alloca %struct.Large
entry:
  %0 = icmp eq i32 %n, 0
  br i1 %0, label %bb1, label %bb2

bb1:     ; preds = %entry
  %v1 = getelementptr inbounds %struct.Large, %struct.Large* %s, i64 0, i32 0, i64 42
  br label %b.exit

bb2:     ; preds = %entry
  %v2 = alloca i8, align 1
  store i8 42, i8* %v2, align 1
 br label %b.exit

b.exit:     ; preds = %bb1, %bb2
  %1 = phi i8* [ %v1, %bb1 ], [ %v2, %bb2 ]
  %2 = load i8, i8* %1, align 1
  store i8 %2, i8* %out, align 1
  ret void
}

; Function call with Large struct
define void @CallInst(%struct.Large* byval(%struct.Large) %s) #0 {
; CHECK-LABEL: @CallInst
; MAX-ARG-LABEL: @CallInst
; CHECK: alloca %struct.Large
; MAX-ARG-LABEL: alloca %struct.Large
  call void @Func(i32 42, %struct.Large* nonnull byval(%struct.Large) %s)
  ret void
}

declare void @Func(i32, %struct.Large* byval(%struct.Large)) local_unnamed_addr #0

attributes #0 = { nounwind "less-precise-fpmad"="false" "frame-pointer"="none" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }

!nvvm.annotations = !{!0}
!0 = !{void (%struct.Large*, i8*)* @ReadFromLargeStruct, !"kernel", i32 1}
!1 = !{void (%struct.Small*, i8*)* @ReadFromSmallStruct, !"kernel", i32 1}
!2 = !{void (%struct.Large*)* @StoreInst, !"kernel", i32 1}
!3 = !{void (%struct.Large*, i1, i8*)* @SelectInst, !"kernel", i32 1}
!4 = !{void (%struct.Large*, i32, i8*)* @PhiNode, !"kernel", i32 1}
!5 = !{void (%struct.Large*)* @CallInst, !"kernel", i32 1}
; facebook end
