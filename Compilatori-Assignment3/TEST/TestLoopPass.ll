; Modulo di test per LoopPass
source_filename = "test.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define i32 @funzione_con_loop(i32 %N, i32 %A) {
entry:
  %cmp4 = icmp sgt i32 %N, 0
  br i1 %cmp4, label %loop.ph, label %exit

loop.ph:                                          ; Preheader
  br label %loop.header

loop.header:                                      ; Header del Loop
  %i.06 = phi i32 [ 0, %loop.ph ], [ %inc, %loop.latch ]
  %res.05 = phi i32 [ 0, %loop.ph ], [ %add1, %loop.latch ]
  
  ; Istruzione Loop Invariant (A * 2 non cambia nel loop)
  %invar = mul nsw i32 %A, 2
  br label %loop.body

loop.body:
  %add1 = add nsw i32 %res.05, %invar
  br label %loop.latch

loop.latch:                                       ; Latch del Loop
  %inc = add nsw i32 %i.06, 1
  %cmp = icmp slt i32 %inc, %N
  br i1 %cmp, label %loop.header, label %exit

exit:                                             ; Exit block
  %res.0.lcssa = phi i32 [ 0, %entry ], [ %add1, %loop.latch ]
  ret i32 %res.0.lcssa
}

define i32 @funzione_senza_loop(i32 %A, i32 %B) {
entry:
  %sum = add nsw i32 %A, %B
  %mult = mul nsw i32 %sum, 3
  ret i32 %mult
}