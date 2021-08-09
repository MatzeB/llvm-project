// RUN: %clang -std=c++17 -emit-ast -o %t.ast %s
// RUN: %clang -std=c++17 -o %t -c %t.ast

template <auto &Tag>
using my_decay = decltype(Tag);

struct Foo {};
constexpr Foo foo{};
void bar(my_decay<foo>) {}
