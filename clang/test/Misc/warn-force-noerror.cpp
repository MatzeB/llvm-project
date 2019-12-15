// facebook T59137049:
// Make sure -Wforce-no-error takes precedence even if there's -Werror after it.
// We need -Wforce-no-error so we can effecitively disable warning as error even
// if fbcode TARGETs enable them explicitly.

// RUN: %clang_cc1 -Wimplicit-int-float-conversion -Wno-error -Wforce-no-error -Werror %s

int main() {
  long i = 222222222222L;
  float a = 222222222222L; // expected-warning {{implicit conversion from 'long' to 'float' changes value from 222222222222 to 222222221312}}
  float b = a + i;
  return (int)b;
}