define void @test_dominator(i32 %cond1, i32 %cond2, i32 %cond3) {
A:
  ; Blocco A: si biforca verso B o C
  %cmpA = icmp eq i32 %cond1, 0
  br i1 %cmpA, label %B, label %C

B:
  ; Blocco B: va direttamente a G
  br label %G

C:
  ; Blocco C: si biforca verso D o E
  %cmpC = icmp eq i32 %cond2, 0
  br i1 %cmpC, label %D, label %E

D:
  ; Blocco D: va a F
  br label %F

E:
  ; Blocco E: va a F
  br label %F

F:
  ; Blocco F: riceve da D ed E, e va a G
  br label %G

G:
  ; Blocco G: riceve da B e da F, ed esce
  ret void
}