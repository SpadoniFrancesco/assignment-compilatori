define dso_local i32 @test_multi_instruction(i32 noundef %arg_b) {
entry:
  ; =========================================================================
  ; TEST 1: Costante a DESTRA nella ADD (a = b + 1, c = a - 1)
  ; L'addizione deve restare. La sottrazione (%c1) deve diventare: %c1 = %arg_b
  ; =========================================================================
  %a1 = add nsw i32 %arg_b, 1
  %c1 = sub nsw i32 %a1, 1

  ; =========================================================================
  ; TEST 2: Costante a SINISTRA nella ADD (a = 1 + b, c = a - 1)
  ; L'addizione deve restare. La sottrazione (%c2) deve diventare: %c2 = %arg_b
  ; =========================================================================
  %a2 = add nsw i32 1, %arg_b
  %c2 = sub nsw i32 %a2, 1

  ; =========================================================================
  ; TEST 3: CONTROLLO (Le costanti NON corrispondono, non deve ottimizzare)
  ; Deve rimanere esattamente così com'è.
  ; =========================================================================
  %a3 = add nsw i32 %arg_b, 5
  %c3 = sub nsw i32 %a3, 2

  ; Somma finale dei risultati per tenere in vita le istruzioni
  %temp = add nsw i32 %c1, %c2
  %final = add nsw i32 %temp, %c3

  ret i32 %final
}