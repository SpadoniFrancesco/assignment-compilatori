; ModuleID = 'test-strength.ll'
source_filename = "test-strength.ll"

define dso_local i32 @test_strength_reduction_diff(i32 noundef %arg_x) {
entry:
  %0 = shl i32 %arg_x, 4
  %1 = sub i32 %0, %arg_x
  %res_non_ottimizzabile = mul nsw i32 %arg_x, 13
  %final = add nsw i32 %1, %res_non_ottimizzabile
  ret i32 %final
}
