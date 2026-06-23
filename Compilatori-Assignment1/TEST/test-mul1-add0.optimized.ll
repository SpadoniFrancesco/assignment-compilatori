; ModuleID = 'test-mul1-add0.ll'
source_filename = "test-mul1-add0.ll"

define dso_local i32 @test_identities(i32 noundef %arg_x, i32 noundef %arg_y, i32 noundef %arg_z) {
entry:
  %check_shl = mul nsw i32 %arg_z, 4
  %check_add_vars = add nsw i32 %arg_x, %arg_y
  %res = mul nsw i32 %check_add_vars, %check_shl
  ret i32 %res
}
