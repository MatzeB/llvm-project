//===- bolt/Passes/AArch64JumpTablePromotion.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file This file declares AArch64JumpTablePromotion pass that
/// widens/re-encodes AArch64 jump tables and rewrites corresponding branch
/// instruction sequences.
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_PASSES_AARCH64JUMPTABLEPROMOTION_H
#define BOLT_PASSES_AARCH64JUMPTABLEPROMOTION_H

#include "bolt/Passes/BinaryPasses.h"

namespace llvm {
namespace bolt {

class AArch64JumpTablePromotion : public BinaryFunctionPass {
public:
  explicit AArch64JumpTablePromotion(const cl::opt<bool> &PrintPass)
      : BinaryFunctionPass(PrintPass) {}

  const char *getName() const override {
    return "aarch64-jump-table-promotion";
  }

  Error runOnFunctions(BinaryContext &BC) override;
};

} // namespace bolt
} // namespace llvm

#endif
