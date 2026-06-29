; ModuleID = './test-clean.ll'
source_filename = "test-loopfusion.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define void @test_fusion(ptr noalias %0, ptr noalias %1, i32 %2) {
entry:
  %cmp.entry = icmp sgt i32 %2, 0
  br i1 %cmp.entry, label %loop0.ph, label %exit

loop0.ph:                                         ; preds = %entry
  br label %loop0.header

loop0.header:                                     ; preds = %loop1.latch, %loop0.ph
  %iv0 = phi i32 [ 0, %loop0.ph ], [ %iv0.next, %loop1.latch ]
  %idx0 = sext i32 %iv0 to i64
  %ptr0 = getelementptr inbounds i32, ptr %0, i64 %idx0
  store i32 0, ptr %ptr0, align 4
  br label %loop0.latch

loop0.latch:                                      ; preds = %loop0.header
  %iv0.next = add nsw i32 %iv0, 1
  %cmp0 = icmp slt i32 %iv0.next, %2
  br label %loop1.header

loop1.ph:                                         ; No predecessors!
  br label %loop1.header

loop1.header:                                     ; preds = %loop0.latch, %loop1.ph
  %idx1 = sext i32 %iv0 to i64
  %ptr1 = getelementptr inbounds i32, ptr %1, i64 %idx1
  store i32 0, ptr %ptr1, align 4
  br label %loop1.latch

loop1.latch:                                      ; preds = %loop1.header
  %iv1.next = add nsw i32 %iv0, 1
  %cmp1 = icmp slt i32 %iv1.next, %2
  br i1 %cmp1, label %loop0.header, label %exit.loopexit

exit.loopexit:                                    ; preds = %loop1.latch
  br label %exit

exit:                                             ; preds = %exit.loopexit, %entry
  ret void
}

define i32 @main() {
entry:
  %a = alloca [1000 x i32], align 16
  %b = alloca [1000 x i32], align 16
  %ptr_a = getelementptr inbounds [1000 x i32], ptr %a, i64 0, i64 0
  %ptr_b = getelementptr inbounds [1000 x i32], ptr %b, i64 0, i64 0
  call void @test_fusion(ptr %ptr_a, ptr %ptr_b, i32 1000)
  ret i32 0
}
