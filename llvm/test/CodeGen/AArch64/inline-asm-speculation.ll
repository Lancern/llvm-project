; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py UTC_ARGS: --version 5
; RUN: llc -mtriple=aarch64 < %s | FileCheck %s
; Make sure we don't speculatively execute inline asm statements.

define dso_local i32 @main(i32 %argc, ptr %argv) {
; CHECK-LABEL: main:
; CHECK:       // %bb.0: // %entry
; CHECK-NEXT:    cmp w0, #3
; CHECK-NEXT:    b.ne .LBB0_2
; CHECK-NEXT:  // %bb.1: // %if.then
; CHECK-NEXT:    //APP
; CHECK-NEXT:    mrs x0, SPSR_EL2
; CHECK-NEXT:    //NO_APP
; CHECK-NEXT:    // kill: def $w0 killed $w0 killed $x0
; CHECK-NEXT:    ret
; CHECK-NEXT:  .LBB0_2: // %if.else
; CHECK-NEXT:    //APP
; CHECK-NEXT:    mrs x0, SPSR_EL1
; CHECK-NEXT:    //NO_APP
; CHECK-NEXT:    // kill: def $w0 killed $w0 killed $x0
; CHECK-NEXT:    ret
entry:
  %cmp = icmp eq i32 %argc, 3
  br i1 %cmp, label %if.then, label %if.else

if.then:
  %0 = tail call i64 asm "mrs $0, SPSR_EL2", "=r"()
  br label %if.end

if.else:
  %1 = tail call i64 asm "mrs $0, SPSR_EL1", "=r"()
  br label %if.end

if.end:
  %y.0.in = phi i64 [ %0, %if.then ], [ %1, %if.else ]
  %y.0 = trunc i64 %y.0.in to i32
  ret i32 %y.0
}
