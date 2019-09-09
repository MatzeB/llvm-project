// facebook T53546053
//===- CFGChangeLogManager.h - Shows CFG change across transformations ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines a class that provices interface for CFG change logging
/// across transformation passes for pass managers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_CFGDIFFINSTRUMENTATIONS_H
#define LLVM_PASSES_CFGDIFFINSTRUMENTATIONS_H

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassInstrumentation.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace llvm {

class CFGChangeLogManager {
public:
  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  // Implementation of pass instrumentation callbacks for new pass manager.
  void runAfterPass(StringRef PassID, Any IR);
};

} // namespace llvm

#endif
