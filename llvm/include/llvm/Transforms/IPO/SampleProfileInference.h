// facebook T68973288
//===- Transforms/IPO/SampleProfileInference.h ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides the interface for the profile inference algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SAMPLEPROFILEINFERENCE_H
#define LLVM_TRANSFORMS_IPO_SAMPLEPROFILEINFERENCE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"

namespace llvm {

using Edge = std::pair<const BasicBlock *, const BasicBlock *>;
using BlockWeightMap = DenseMap<const BasicBlock *, uint64_t>;
using EdgeWeightMap = DenseMap<Edge, uint64_t>;
using BlockEdgeMap =
    DenseMap<const BasicBlock *, SmallVector<const BasicBlock *, 8>>;

/// Sample profile inference pass.
class SampleProfileInference {
public:
  SampleProfileInference(Function &F, BlockEdgeMap &Successors,
                         BlockWeightMap &SampleBlockWeights)
      : F(F), Successors(Successors), SampleBlockWeights(SampleBlockWeights) {}

  void apply(BlockWeightMap &BlockWeights, EdgeWeightMap &EdgeWeights);

private:
  /// Function.
  const Function &F;

  /// Successors for each basic block in the CFG.
  BlockEdgeMap &Successors;

  /// Map basic blocks to their sampled weights.
  BlockWeightMap &SampleBlockWeights;
};

} // end namespace llvm
#endif // LLVM_TRANSFORMS_IPO_SAMPLEPROFILEINFERENCE_H
