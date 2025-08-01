; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; REQUIRES: asserts

; RUN: opt -passes=loop-vectorize -force-vector-interleave=1 -vectorizer-maximize-bandwidth -mtriple=arm64-apple-ios -debug -S %s 2>&1 | FileCheck %s

target triple = "arm64-apple-ios"

; CHECK-LABEL: LV: Checking a loop in 'test'
; CHECK:      VPlan 'Initial VPlan for VF={2},UF>=1' {
; CHECK-NEXT: Live-in vp<[[VF:%.+]]> = VF
; CHECK-NEXT: Live-in vp<[[VFxUF:%.+]]> = VF * UF
; CHECK-NEXT: Live-in vp<[[VTC:%.+]]> = vector-trip-count

; CHECK-NEXT: Live-in ir<1024> = original trip-count
; CHECK-EMPTY:
; CHECK-NEXT: ir-bb<entry>:
; CHECK-NEXT: Successor(s): scalar.ph, vector.ph
; CHECK-EMPTY:
; CHECK-NEXT: vector.ph:
; CHECK-NEXT: Successor(s): vector loop
; CHECK-EMPTY:
; CHECK-NEXT: <x1> vector loop: {
; CHECK-NEXT:   vector.body:
; CHECK-NEXT:     EMIT vp<[[CAN_IV:%.+]]> = CANONICAL-INDUCTION
; CHECK-NEXT:     vp<[[STEPS:%.+]]>    = SCALAR-STEPS vp<[[CAN_IV]]>, ir<1>, vp<[[VF]]>
; CHECK-NEXT:     CLONE ir<%gep.src> = getelementptr inbounds ir<%src>, vp<[[STEPS]]>
; CHECK-NEXT:     vp<[[VEC_PTR:%.+]]> = vector-pointer ir<%gep.src>
; CHECK-NEXT:     WIDEN ir<%l> = load vp<[[VEC_PTR]]>
; CHECK-NEXT:     WIDEN-CAST ir<%conv> = fpext ir<%l> to double
; CHECK-NEXT:     WIDEN-CALL ir<%s> = call fast @llvm.sin.f64(ir<%conv>) (using library function: __simd_sin_v2f64)
; CHECK-NEXT:     REPLICATE ir<%gep.dst> = getelementptr inbounds ir<%dst>, vp<[[STEPS]]>
; CHECK-NEXT:     REPLICATE store ir<%s>, ir<%gep.dst>
; CHECK-NEXT:     EMIT vp<[[CAN_IV_NEXT:%.+]]> = add nuw vp<[[CAN_IV]]>, vp<[[VFxUF]]>
; CHECK-NEXT:     EMIT branch-on-count vp<[[CAN_IV_NEXT]]>, vp<[[VTC]]>
; CHECK-NEXT:   No successors
; CHECK-NEXT: }
; CHECK-NEXT: Successor(s): middle.block
; CHECK-EMPTY:
; CHECK-NEXT: middle.block:
; CHECK-NEXT:   EMIT vp<[[CMP:%.+]]> = icmp eq ir<1024>, vp<[[VTC]]>
; CHECK-NEXT:   EMIT branch-on-cond vp<[[CMP]]>
; CHECK-NEXT: Successor(s): ir-bb<exit>, scalar.ph
; CHECK-EMPTY:
; CHECK-NEXT: ir-bb<exit>:
; CHECK-NEXT: No successors
; CHECK-EMPTY:
; CHECK-NEXT: scalar.ph:
; CHECK-NEXT:   EMIT-SCALAR vp<[[RESUME:%.+]]> = phi [ vp<[[VTC]]>, middle.block ], [ ir<0>, ir-bb<entry> ]
; CHECK-NEXT: Successor(s): ir-bb<loop>
; CHECK-EMPTY:
; CHECK-NEXT: ir-bb<loop>:
; CHECK-NEXT:   IR   %iv = phi i64 [ 0, %entry ], [ %iv.next, %loop ] (extra operand: vp<[[RESUME]]> from scalar.ph)
; CHECK:        IR   %cmp = icmp ne i64 %iv.next, 1024
; CHECK-NEXT: No successors
; CHECK-NEXT: }

; CHECK:      VPlan 'Initial VPlan for VF={4},UF>=1' {
; CHECK-NEXT: Live-in vp<[[VF:%.+]]> = VF
; CHECK-NEXT: Live-in vp<[[VFxUF:%.+]]> = VF * UF
; CHECK-NEXT: Live-in vp<[[VTC:%.+]]> = vector-trip-count
; CHECK-NEXT: Live-in ir<1024> = original trip-count
; CHECK-EMPTY:
; CHECK-NEXT: ir-bb<entry>:
; CHECK-NEXT: Successor(s): scalar.ph, vector.ph
; CHECK-EMPTY:
; CHECK-NEXT: vector.ph:
; CHECK-NEXT: Successor(s): vector loop
; CHECK-EMPTY:
; CHECK-NEXT: <x1> vector loop: {
; CHECK-NEXT:   vector.body:
; CHECK-NEXT:     EMIT vp<[[CAN_IV:%.+]]> = CANONICAL-INDUCTION
; CHECK-NEXT:     vp<[[STEPS:%.+]]>    = SCALAR-STEPS vp<[[CAN_IV]]>, ir<1>, vp<[[VF]]>
; CHECK-NEXT:     CLONE ir<%gep.src> = getelementptr inbounds ir<%src>, vp<[[STEPS]]>
; CHECK-NEXT:     vp<[[VEC_PTR:%.+]]> = vector-pointer ir<%gep.src>
; CHECK-NEXT:     WIDEN ir<%l> = load vp<[[VEC_PTR]]>
; CHECK-NEXT:     WIDEN-CAST ir<%conv> = fpext ir<%l> to double
; CHECK-NEXT:     WIDEN-INTRINSIC ir<%s> = call fast llvm.sin(ir<%conv>)
; CHECK-NEXT:     REPLICATE ir<%gep.dst> = getelementptr inbounds ir<%dst>, vp<[[STEPS]]>
; CHECK-NEXT:     REPLICATE store ir<%s>, ir<%gep.dst>
; CHECK-NEXT:     EMIT vp<[[CAN_IV_NEXT:%.+]]> = add nuw vp<[[CAN_IV]]>, vp<[[VFxUF]]>
; CHECK-NEXT:     EMIT branch-on-count vp<[[CAN_IV_NEXT]]>, vp<[[VTC]]>
; CHECK-NEXT:   No successors
; CHECK-NEXT: }
; CHECK-NEXT: Successor(s): middle.block
; CHECK-EMPTY:
; CHECK-NEXT: middle.block:
; CHECK-NEXT:   EMIT vp<[[CMP:%.+]]> = icmp eq ir<1024>, vp<[[VTC]]>
; CHECK-NEXT:   EMIT branch-on-cond vp<[[CMP]]>
; CHECK-NEXT: Successor(s): ir-bb<exit>, scalar.ph
; CHECK-EMPTY:
; CHECK-NEXT: ir-bb<exit>:
; CHECK-NEXT: No successors
; CHECK-EMPTY:
; CHECK-NEXT: scalar.ph:
; CHECK-NEXT:   EMIT-SCALAR vp<[[RESUME:%.+]]> = phi [ vp<[[VTC]]>, middle.block ], [ ir<0>, ir-bb<entry> ]
; CHECK-NEXT: Successor(s): ir-bb<loop>
; CHECK-EMPTY:
; CHECK-NEXT: ir-bb<loop>:
; CHECK-NEXT:   IR   %iv = phi i64 [ 0, %entry ], [ %iv.next, %loop ] (extra operand: vp<[[RESUME]]> from scalar.ph)
; CHECK:        IR   %cmp = icmp ne i64 %iv.next, 1024
; CHECK-NEXT: No successors
; CHECK-NEXT: }
;
;
define void @test(ptr noalias %src, ptr noalias %dst) {
; CHECK-LABEL: @test(
; CHECK:       vector.body:
; CHECK-NEXT:    [[INDEX:%.*]] = phi i64 [ 0, %vector.ph ], [ [[INDEX_NEXT:%.*]], %vector.body ]
; CHECK-NEXT:    [[TMP0:%.*]] = add i64 [[INDEX]], 0
; CHECK-NEXT:    [[TMP1:%.*]] = add i64 [[INDEX]], 1
; CHECK-NEXT:    [[TMP2:%.*]] = getelementptr inbounds float, ptr [[SRC:%.*]], i64 [[TMP0]]
; CHECK-NEXT:    [[WIDE_LOAD:%.*]] = load <2 x float>, ptr [[TMP2]], align 4
; CHECK-NEXT:    [[TMP4:%.*]] = fpext <2 x float> [[WIDE_LOAD]] to <2 x double>
; CHECK-NEXT:    [[TMP5:%.*]] = call fast <2 x double> @__simd_sin_v2f64(<2 x double> [[TMP4]])
; CHECK-NEXT:    [[TMP6:%.*]] = getelementptr inbounds float, ptr [[DST:%.*]], i64 [[TMP0]]
; CHECK-NEXT:    [[TMP7:%.*]] = getelementptr inbounds float, ptr [[DST]], i64 [[TMP1]]
; CHECK-NEXT:    [[TMP8:%.*]] = extractelement <2 x double> [[TMP5]], i32 0
; CHECK-NEXT:    store double [[TMP8]], ptr [[TMP6]], align 8
; CHECK-NEXT:    [[TMP9:%.*]] = extractelement <2 x double> [[TMP5]], i32 1
; CHECK-NEXT:    store double [[TMP9]], ptr [[TMP7]], align 8
; CHECK-NEXT:    [[INDEX_NEXT]] = add nuw i64 [[INDEX]], 2
; CHECK-NEXT:    [[TMP10:%.*]] = icmp eq i64 [[INDEX_NEXT]], 1024
; CHECK-NEXT:    br i1 [[TMP10]], label %middle.block, label %vector.body
;
entry:
  br label %loop

loop:
  %iv = phi i64 [ 0, %entry ], [ %iv.next, %loop ]
  %gep.src = getelementptr inbounds float, ptr %src, i64 %iv
  %l = load float, ptr %gep.src, align 4
  %conv = fpext float %l to double
  %s = call fast double @llvm.sin.f64(double %conv) #0
  %gep.dst = getelementptr inbounds float, ptr %dst, i64 %iv
  store double %s, ptr %gep.dst
  %iv.next = add nsw i64 %iv, 1
  %cmp = icmp ne i64 %iv.next, 1024
  br i1 %cmp, label %loop, label %exit

exit:
  ret void
}

declare double @llvm.sin.f64(double)

declare <2 x double> @__simd_sin_v2f64(<2 x double>)

attributes #0 = { "vector-function-abi-variant"="_ZGV_LLVM_N2v_llvm.sin.f64(__simd_sin_v2f64)" }
