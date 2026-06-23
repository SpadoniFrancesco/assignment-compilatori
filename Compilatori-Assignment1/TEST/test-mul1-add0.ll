define dso_local i32 @test_identities(i32 noundef %arg_x, i32 noundef %arg_y, i32 noundef %arg_z) {
entry:
  %opt_mul = mul nsw i32 %arg_x, 1
  %opt_add = add nsw i32 %arg_y, 0
  %check_shl = mul nsw i32 %arg_z, 4
  %check_add_vars = add nsw i32 %opt_mul, %opt_add
  %res = mul nsw i32 %check_add_vars, %check_shl
  ret i32 %res
}