// facebook T60099708
// RUN: rm -rf %t
// RUN: %clang_cc1 -O2 -mllvm -opt-cxxconstruct-limit=1 -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK-OPTNONE
// RUN: %clang_cc1 -O2 -mllvm -opt-cxxconstruct-limit=1 -emit-llvm -o - %s 2>&1 | FileCheck %s --check-prefix=CHECK-WARNING
// RUN: %clang_cc1 -O2 -mllvm -opt-cxxconstruct-limit=100 -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK-NOOPTNONE

struct MyStructType
{
  int _a;
  int _b;

  MyStructType(int a, int b)
  {
    _a = a;
    _b = b;
  }
};

// CHECK-OPTNONE-DAG: {{.*}} @_Z6GetMapv({{.*}} #[[OPTNONE:.*]] {
// CHECK-NOOPTNONE-DAG: {{.*}} @_Z6GetMapv({{.*}} #[[OPTNONE:.*]] {

const MyStructType* GetMap() {

  static MyStructType underlying_map[] = {
       {100, 191}, {200, 202}};

  return underlying_map;
};

// CHECK-OPTNONE: attributes #[[OPTNONE]] = {{.*}} optnone
// CHECK-NOOPTNONE-NOT: attributes #[[OPTNONE]] = {{.*}} optnone
// CHECK-WARNING: warning: Function _Z6GetMapv is too big to optimize
