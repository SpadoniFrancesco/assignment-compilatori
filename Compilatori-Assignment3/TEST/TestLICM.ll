define i32 @test_licm(i32 %N, i32 %A) {
entry:
  %cmp4 = icmp sgt i32 %N, 0
  br i1 %cmp4, label %loop.ph, label %exit

loop.ph:
  br label %loop.header

loop.header:
  %i.06 = phi i32 [ 0, %loop.ph ], [ %inc, %loop.latch ]
  %res.05 = phi i32 [ 0, %loop.ph ], [ %add1, %loop.latch ]
  
  ; Istruzioni Loop Invariant
  %invar = mul nsw i32 %A, 2
  %invar_dep = add nsw i32 %invar, 5
  
  br label %loop.body

loop.body:
  %add1 = add nsw i32 %res.05, %invar_dep
  br label %loop.latch

loop.latch:
  %inc = add nsw i32 %i.06, 1
  %cmp = icmp slt i32 %inc, %N
  br i1 %cmp, label %loop.header, label %exit.loopexit

exit.loopexit:
  br label %exit

exit:
  %res.0.lcssa = phi i32 [ 0, %entry ], [ %add1, %exit.loopexit ]
  ret i32 %res.0.lcssa
}