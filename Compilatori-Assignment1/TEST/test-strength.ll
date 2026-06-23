define dso_local i32 @test_strength_reduction(i32 noundef %arg_x, i32 noundef %arg_y) {
entry:
  %res_destra = mul nsw i32 %arg_x, 15
  %res_sinistra = mul nsw i32 15, %arg_y
  %controllo = mul nsw i32 %arg_x, %arg_y
  %temp = add nsw i32 %res_destra, %res_sinistra
  %final = add nsw i32 %temp, %controllo
  ret i32 %final
}