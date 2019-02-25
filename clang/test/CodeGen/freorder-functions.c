// facebook t19574305
// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-pc-linux -S -O3 -flegacy-pass-manager -ffunction-sections -fprofile-sample-use=%S/Inputs/freorder-functions.prof -o - < %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-linux -S -O3 -flegacy-pass-manager -ffunction-sections -fprofile-sample-use=%S/Inputs/freorder-functions.prof -fno-reorder-functions -o - < %s | FileCheck --check-prefix=CHECK-NOPREFIX %s

// opt tool option precedes driver option.
// RUN: %clang_cc1 -triple x86_64-pc-linux -S -O3 -flegacy-pass-manager -ffunction-sections -fprofile-sample-use=%S/Inputs/freorder-functions.prof -fno-reorder-functions -mllvm -profile-guided-section-prefix=true -o - < %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-linux -S -O3 -flegacy-pass-manager -ffunction-sections -fprofile-sample-use=%S/Inputs/freorder-functions.prof -freorder-functions -mllvm -profile-guided-section-prefix=false -o - < %s | FileCheck --check-prefix=CHECK-NOPREFIX %s

void hot_func() {
  return;
}

void cold_func() {
  hot_func();
  return;
}

// CHECK: .section .text.hot.hot_func,"ax",@progbits
// CHECK: .section .text.unlikely.cold_func,"ax",@progbits
// CHECK-NOPREFIX: .section .text.hot_func,"ax",@progbits
// CHECK-NOPREFIX: .section .text.cold_func,"ax",@progbits
