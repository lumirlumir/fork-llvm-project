; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -verify-machineinstrs -mtriple=powerpc64le-unknown-linux-gnu \
; RUN:     -ppc-asm-full-reg-names -mcpu=pwr10 < %s | \
; RUN:     FileCheck %s --check-prefix=CHECK-LINUX
; RUN: llc -verify-machineinstrs -mtriple=powerpc64-unknown-linux-gnu \
; RUN:     -ppc-asm-full-reg-names -mcpu=pwr10 < %s | \
; RUN:     FileCheck %s --check-prefix=CHECK-LINUX-BE
; RUN: llc -verify-machineinstrs -mtriple=powerpc64-ibm-aix-xcoff \
; RUN:     -ppc-asm-full-reg-names -mcpu=pwr10 < %s | \
; RUN:     FileCheck %s --check-prefix=CHECK-AIX
; RUN: llc -verify-machineinstrs -mtriple=powerpc-unknown-linux-gnu \
; RUN:     -ppc-asm-full-reg-names -mcpu=pwr10 < %s | \
; RUN:     FileCheck %s --check-prefix=CHECK-LINUX-32
; RUN: llc -verify-machineinstrs -mtriple=powerpc-ibm-aix-xcoff \
; RUN:     -ppc-asm-full-reg-names -mcpu=pwr10 < %s | \
; RUN:     FileCheck %s --check-prefix=CHECK-AIX-32

declare hidden i32 @call1()
define hidden void @function1() {
; CHECK-LINUX-LABEL: function1:
; CHECK-LINUX:       # %bb.0: # %entry
; CHECK-LINUX-NEXT:    mflr r0
; CHECK-LINUX-NEXT:    std r0, 16(r1)
; CHECK-LINUX-NEXT:    stdu r1, -32(r1)
; CHECK-LINUX-NEXT:    .cfi_def_cfa_offset 32
; CHECK-LINUX-NEXT:    .cfi_offset lr, 16
; CHECK-LINUX-NEXT:    bl call1@notoc
; CHECK-LINUX-NEXT:    addi r1, r1, 32
; CHECK-LINUX-NEXT:    ld r0, 16(r1)
; CHECK-LINUX-NEXT:    mtlr r0
; CHECK-LINUX-NEXT:    blr
;
; CHECK-LINUX-BE-LABEL: function1:
; CHECK-LINUX-BE:       # %bb.0: # %entry
; CHECK-LINUX-BE-NEXT:    mflr r0
; CHECK-LINUX-BE-NEXT:    std r0, 16(r1)
; CHECK-LINUX-BE-NEXT:    stdu r1, -112(r1)
; CHECK-LINUX-BE-NEXT:    .cfi_def_cfa_offset 112
; CHECK-LINUX-BE-NEXT:    .cfi_offset lr, 16
; CHECK-LINUX-BE-NEXT:    bl call1
; CHECK-LINUX-BE-NEXT:    nop
; CHECK-LINUX-BE-NEXT:    addi r1, r1, 112
; CHECK-LINUX-BE-NEXT:    ld r0, 16(r1)
; CHECK-LINUX-BE-NEXT:    mtlr r0
; CHECK-LINUX-BE-NEXT:    blr
;
; CHECK-AIX-LABEL: function1:
; CHECK-AIX:       # %bb.0: # %entry
; CHECK-AIX-NEXT:    mflr r0
; CHECK-AIX-NEXT:    std r0, 16(r1)
; CHECK-AIX-NEXT:    stdu r1, -112(r1)
; CHECK-AIX-NEXT:    bl .call1[PR]
; CHECK-AIX-NEXT:    nop
; CHECK-AIX-NEXT:    addi r1, r1, 112
; CHECK-AIX-NEXT:    ld r0, 16(r1)
; CHECK-AIX-NEXT:    mtlr r0
; CHECK-AIX-NEXT:    blr
;
; CHECK-LINUX-32-LABEL: function1:
; CHECK-LINUX-32:       # %bb.0: # %entry
; CHECK-LINUX-32-NEXT:    mflr r0
; CHECK-LINUX-32-NEXT:    stw r0, 4(r1)
; CHECK-LINUX-32-NEXT:    stwu r1, -32(r1)
; CHECK-LINUX-32-NEXT:    .cfi_def_cfa_offset 32
; CHECK-LINUX-32-NEXT:    .cfi_offset lr, 4
; CHECK-LINUX-32-NEXT:    bl call1
; CHECK-LINUX-32-NEXT:    stw r3, 16(r1)
; CHECK-LINUX-32-NEXT:    lwz r0, 36(r1)
; CHECK-LINUX-32-NEXT:    addi r1, r1, 32
; CHECK-LINUX-32-NEXT:    mtlr r0
; CHECK-LINUX-32-NEXT:    blr
;
; CHECK-AIX-32-LABEL: function1:
; CHECK-AIX-32:       # %bb.0: # %entry
; CHECK-AIX-32-NEXT:    mflr r0
; CHECK-AIX-32-NEXT:    stw r0, 8(r1)
; CHECK-AIX-32-NEXT:    stwu r1, -80(r1)
; CHECK-AIX-32-NEXT:    bl .call1[PR]
; CHECK-AIX-32-NEXT:    nop
; CHECK-AIX-32-NEXT:    stw r3, 64(r1)
; CHECK-AIX-32-NEXT:    addi r1, r1, 80
; CHECK-AIX-32-NEXT:    lwz r0, 8(r1)
; CHECK-AIX-32-NEXT:    mtlr r0
; CHECK-AIX-32-NEXT:    blr
entry:
  %tailcall1 = tail call i32 @call1()
  %0 = insertelement <4 x i32> poison, i32 %tailcall1, i64 1
  %1 = insertelement <4 x i32> %0, i32 0, i64 2
  %2 = insertelement <4 x i32> %1, i32 0, i64 3
  %3 = trunc <4 x i32> %2 to <4 x i8>
  %4 = icmp eq <4 x i8> %3, zeroinitializer
  %5 = shufflevector <4 x i1> %4, <4 x i1> poison, <2 x i32> <i32 3, i32 undef>
  %6 = shufflevector <4 x i1> %4, <4 x i1> poison, <2 x i32> <i32 2, i32 undef>
  %7 = xor <2 x i1> %5, <i1 true, i1 poison>
  %8 = shufflevector <2 x i1> %7, <2 x i1> poison, <2 x i32> zeroinitializer
  %9 = zext <2 x i1> %8 to <2 x i64>
  %10 = xor <2 x i1> %6, <i1 true, i1 poison>
  %11 = shufflevector <2 x i1> %10, <2 x i1> poison, <2 x i32> zeroinitializer
  %12 = zext <2 x i1> %11 to <2 x i64>
  br label %next_block

next_block:
  %13 = add <2 x i64> zeroinitializer, %9
  %14 = add <2 x i64> zeroinitializer, %12
  %shift704 = shufflevector <2 x i64> %13, <2 x i64> poison, <2 x i32> <i32 1, i32 undef>
  %15 = add <2 x i64> %shift704, %13
  %shift705 = shufflevector <2 x i64> %14, <2 x i64> poison, <2 x i32> <i32 1, i32 undef>
  %16 = add <2 x i64> %shift705, %14
  ret void
}
