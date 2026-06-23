; ModuleID = 'test-strength.ll'
source_filename = "test-strength.ll"

define dso_local i32 @test_strength_reduction(i32 noundef %arg_x, i32 noundef %arg_y) {
entry:
  %0 = shl i32 %arg_x, 4
  %1 = mul i32 %arg_x, 1
  %2 = sub i32 %0, %1
  %3 = shl i32 %arg_y, 4
  %4 = mul i32 %arg_y, 1
  %5 = sub i32 %3, %4
  %controllo = mul nsw i32 %arg_x, %arg_y
  %temp = add nsw i32 %2, %5
  %final = add nsw i32 %temp, %controllo
  ret i32 %final
}
