; ModuleID = 'test-multi-instruction.ll'
source_filename = "test-multi-instruction.ll"

define dso_local i32 @test_multi_instruction(i32 noundef %arg_b) {
entry:
  %a1 = add nsw i32 %arg_b, 1
  %a2 = add nsw i32 1, %arg_b
  %a3 = add nsw i32 %arg_b, 5
  %c3 = sub nsw i32 %a3, 2
  %temp = add nsw i32 %arg_b, %arg_b
  %final = add nsw i32 %temp, %c3
  ret i32 %final
}
