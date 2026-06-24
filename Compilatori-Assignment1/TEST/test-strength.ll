define dso_local i32 @test_strength_reduction_diff(i32 noundef %arg_x) {
entry:
  ; =========================================================================
  ; TEST 1: Moltiplicazione per 15 (diff = 16 - 15 = 1)
  ; DEVE ESSERE OTTIMIZZATA in: (x << 4) - x
  ; =========================================================================
  %res_ottimizzabile = mul nsw i32 %arg_x, 15

  ; =========================================================================
  ; TEST 2: Moltiplicazione per 13 (diff = 16 - 13 = 3)
  ; NON DEVE ESSERE TOCCATA (rimane mul) per evitare introduzione di nuove MUL
  ; =========================================================================
  %res_non_ottimizzabile = mul nsw i32 %arg_x, 13

  ; Somma finale per tenere in vita i risultati
  %final = add nsw i32 %res_ottimizzabile, %res_non_ottimizzabile
  ret i32 %final
}