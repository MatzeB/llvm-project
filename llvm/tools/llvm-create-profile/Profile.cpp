// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Class to represent source level profile.
// facebook T26943842

#include "Profile.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "SampleReader.h"
#include "SymbolMap.h"
#include "llvm/Support/CommandLine.h"
#include <fstream>
#include <sstream>
#define DEBUG_TYPE "autofdo_profile"
llvm::cl::opt<bool> UseLbr("use-lbr",
                           llvm::cl::desc("Whether to use lbr profile."),
                           llvm::cl::init(true));

namespace autofdo {
using namespace std;


/// Add the sample count of a given assembly instructions to the map
void Profile::AddRange(const Range &range, uint64_t count) {
  for (auto inst = range.begin; inst.offset < range.end.offset; ++inst) {
    AddInstruction(inst, count);
  }
}
void Profile::AddInstruction(const autofdo::InstructionLocation &inst, uint64_t count) {

    LLVM_DEBUG(std::cout << std::hex << inst << std::dec);
    LLVM_DEBUG(std::cout << " --> " << count << std::endl);
    Symbol *root_symbol =
            symbol_map_.findSymbolFromLocation(inst);
    if (root_symbol == nullptr) {
        return;
    }
    const auto & sourceInfo = symbolizer.symbolizeInstruction(inst);

    if (sourceInfo.getNumberOfFrames() > 0) {
        auto duplicationFactor = InstructionSymbolizer::getDuplicationFactor(
                sourceInfo.getFrame(0));
        LLVM_DEBUG(std::cout << std::hex << inst << std::dec);
        LLVM_DEBUG(std::cout << " --> ";
                           symbolizer.print(sourceInfo, std::cout));
        LLVM_DEBUG(std::cout << " --> "
                             << count * duplicationFactor
                             << std::endl);
        LLVM_DEBUG(std::cout << " --> "
                             << symbolizer.getOffset(sourceInfo.getFrame(0))
                             << std::endl);
        symbol_map_.AddSourceCount(inst, sourceInfo,
                                   count * duplicationFactor, 0,
                                   SymbolMap::MAX);
    }
}
/// Add the samples in the given maps in the contained SymbolMap
void Profile::ComputeProfile(const RangeCountMap &range_map,
                             const AddressCountMap &address_map,
                             const BranchCountMap &branch_map) {

  AddressCountMap map;
  const AddressCountMap *map_ptr;
  if (UseLbr) {
    if (range_map.size() == 0) {
      return;
    }
    LLVM_DEBUG(std::cout << "adding ranges to the map" << std::endl);
    for (const auto &range_count : range_map) {
      LLVM_DEBUG(std::cout << "Range : " << range_count.first << std::endl);
      LLVM_DEBUG(std::cout << "Range length : "
                           << range_count.first.end - range_count.first.begin
                           << std::endl);
      LLVM_DEBUG(std::cout << "Range count: " << range_count.second
                           << std::endl);
      LLVM_DEBUG(std::cout << range_count.first << std::endl);
      // We only compare offset, because begin and end have the same ObjectFile
      for (InstructionLocation loc = range_count.first.begin;
           loc < range_count.first.end; ++loc) {
        LLVM_DEBUG(std::cout << std::hex << loc << std::dec << " --> "
                             << range_count.second << std::endl);
        map[loc] += range_count.second;
      }
    }
    map_ptr = &map;
  } else {
    map_ptr = &address_map;
  }

  LLVM_DEBUG(std::cout << "-addresses-" << std::endl);

  for (const auto &address_count : *map_ptr) {
      auto & inst = address_count.first ;
      auto & count = address_count.second;
      AddInstruction(inst,count);
  }

  LLVM_DEBUG(std::cout << "-branches-" << std::endl);
  for (const auto &branch_count : branch_map) {
    auto &branch = branch_count.first;

    LLVM_DEBUG(std::cout << "Processing : " << std::hex << branch << std::dec
                         << std::endl);

    const auto &instInfo = symbolizer.symbolizeInstruction(branch.instruction);

    LLVM_DEBUG(std::cout << "Found branch target" << std::hex
                         << branch_count.first << std::dec << std::endl);

    // Skip branches between different ranges of a symbol
    if (!symbol_map_.IsCrossFunctionBranch(branch))
      continue;

    // If the call target is a coroutine .resume function, in the debug info
    // this function is mapped to the ramp function. We don't want the
    // calling stats to be merged into the ramp function, to avoid confusing
    // the inliner.
    if (symbol_map_.IsCallToCoroutineResume(branch.target))
      continue;

    // If the target is a known function, add to its head_count
    symbol_map_.AddSymbolEntryCount(branch_count.first.target,
                                    branch_count.second);

    auto callee = symbol_map_.map().find(branch.target);
    if (callee != symbol_map_.map().end()) {
      LLVM_DEBUG(std::cout << " --> " << branch_count.second << std::endl);
      symbol_map_.AddIndirectCallTarget(branch, instInfo, branch_count.second);
    }
  }

  symbol_map_.MergeSplitFunctions();

  symbol_map_.CalculateThresholdFromTotalCount();

  LLVM_DEBUG(std::cout << std::cout.rdbuf());
}

Profile::~Profile() {}
} // namespace autofdo
