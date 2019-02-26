// facebook begin D8040619
// RUN: %clang_cc1 -fsyntax-only -Wuninitialized -verify %s
// expected-no-diagnostics

class Foo {
  int x;

public:
  Foo(int _x) : x(_x) {}
};

class Bar : public Foo {
  int y;
  int init(int i) { return i; }

public:
  Bar(int _y) : Foo(y = init(_y) ? y + 1 : 0) {}
};

// facebook end D8040619
