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

// Class to represent the symbol map.
// facebook T26943842
#include <algorithm>
#include <map>
#include <set>

#include "Profile.h"

#include "InstructionSymbolizer.h"
#include "SymbolMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include <iostream>

#define DEBUG_TYPE "autofdo_symbol_map"

llvm::cl::opt<double> SampleThresholdFrac(
    "sample_threshold_frac",
    llvm::cl::desc(
        "Sample threshold ratio. The threshold of total function count is "
        "determined by max_sample_count * sample_threshold_frac."),
    llvm::cl::init(0.000000));

llvm::cl::opt<uint64_t> SampleThresholdAbsl(
    "sample_threshold_absolute",
    llvm::cl::desc(
        "Absolute Sample threshold. Functions with a total_count "
        " less than threshold are not included in the final profile"),
    llvm::cl::init(10));

namespace {
using namespace std;

// Prints some blank space for identation.
void Identation(int ident) {
  for (int i = 0; i < ident; i++) {
    printf(" ");
  }
}

void PrintSourceLocation(uint32_t start_line, uint32_t offset, int ident) {
  Identation(ident);
  if (offset & 0xffff) {
    printf("%u.%u: ", (offset >> 16) + start_line, offset & 0xffff);
  } else {
    printf("%u: ", (offset >> 16) + start_line);
  }
}
} // namespace

namespace autofdo {
ProfileInfo &ProfileInfo::operator+=(const ProfileInfo &s) {
  count += s.count;
  num_inst += s.num_inst;
  for (const auto &target_count : s.target_map) {
    target_map[target_count.first] += target_count.second;
  }
  return *this;
}

struct TargetCountCompare {
  bool operator()(const TargetCountPair &t1, const TargetCountPair &t2) const {
    if (t1.second != t2.second) {
      return t1.second > t2.second;
    } else {
      return t1.first > t2.first;
    }
  }
};

void GetSortedTargetCountPairs(const CallTargetCountMap &call_target_count_map,
                               TargetCountPairs *target_counts) {
  for (const auto &name_count : call_target_count_map) {
    target_counts->push_back(name_count);
  }
  std::sort(target_counts->begin(), target_counts->end(), TargetCountCompare());
}

SymbolMap::~SymbolMap() {
  // Different keys (function names) may map to a same symbol.
  // In order to prevent double free, we first merge all symbols
  // into a set, then remove every symbol from the set.
  set<Symbol *> delete_set;
  for (LocationSymbolMap::iterator iter = map_.begin(); iter != map_.end();
       ++iter) {
    delete_set.insert(iter->second);
  }
  for (const auto &symbol : delete_set) {
    delete symbol;
  }
}

Symbol::~Symbol() {
  for (auto &callsite_symbol : callsites) {
    delete callsite_symbol.second;
  }
}

void Symbol::Merge(const Symbol *other) {
  total_count += other->total_count;
  head_count += other->head_count;
  if (info.FileName.empty()) {
    info.FileName = other->info.FileName;
    // info.dir_name = other->info;
  }
  for (const auto &pos_count : other->pos_counts)
    pos_counts[pos_count.first] += pos_count.second;
  // Traverses all callsite, recursively Merge the callee symbol.
  for (const auto &callsite_symbol : other->callsites) {
    std::pair<CallsiteMap::iterator, bool> ret =
        callsites.insert(CallsiteMap::value_type(callsite_symbol.first, NULL));
    // If the callsite does not exist in the current symbol, create a
    // new callee symbol with the clone's function name.
    if (ret.second) {
      ret.first->second = new Symbol();
      ret.first->second->info.FunctionName = ret.first->first.second;
    }
    ret.first->second->Merge(callsite_symbol.second);
  }
}

void SymbolMap::MergeSplitFunctions() {
  for (auto &group : symbol_groups_) {
    if (group.size() <= 1)
      continue;
    // Merge all ranges into the master range which is the first range seen to
    // have a non-null profile.
    uint64_t MasterIdx = 0;
    // Keep the sum of sizes of all ranges.
    uint64_t SizeSum = group[0].Size;
    auto masterProfile = FindSymbol(group[0].StartAddress);
    for (unsigned i = 1; i < group.size(); i++) {
      SizeSum += group[i].Size;
      auto profile = FindSymbol(group[i].StartAddress);
      if (!profile)
        continue;
      if (masterProfile) {
        assert(group[i].Name == group[MasterIdx].Name);
        LLVM_DEBUG(std::cout << " Merging Profile : " << group[i].Name << " "
                             << group[MasterIdx].StartAddress << " "
                             << group[i].StartAddress << std::endl);
        assert(profile->head_count == 0);
        masterProfile->Merge(profile);
        RemoveSymbol(group[i].StartAddress);
      } else {
        masterProfile = profile;
        MasterIdx = i;
      }
    }
    // Update the merged size of the master range to be the total size of all
    // ranges.
    group[MasterIdx].MergedSize = SizeSum;
  }
}

void SymbolMap::AddSymbol(const InstructionLocation &loc,
                          const std::string &name) {
  std::pair<LocationSymbolMap::iterator, bool> ret =
      map_.insert(LocationSymbolMap::value_type(loc, NULL));
  if (ret.second) {
    DILineInfo info;
    info.FunctionName = name;
    ret.first->second = new Symbol(std::move(info));
  }
}

Symbol *SymbolMap::FindSymbol(const InstructionLocation &loc) const {
  auto iter = map_.find(loc);
  return iter != map_.end() ? iter->second : nullptr;
}

void SymbolMap::RemoveSymbol(const InstructionLocation &loc) {
  map_.erase(loc);
}

bool SymbolMap::IsCrossFunctionBranch(const Branch &branch) const {
  auto iter = address_symbol_map_.find(branch.target);
  // Skip local branches
  if (iter == address_symbol_map_.end())
    return false;
  // It's guaranteed that the first entry in the group is always the orginal
  // function entry (master) and other entires cannot be reached directly
  // externally without going through the master entry.
  auto TargetSym = &iter->second;
  auto &Group = symbol_groups_[TargetSym->GroupId - 1];
  return TargetSym == &Group[0];
}

bool SymbolMap::IsCallToCoroutineResume(
    const InstructionLocation &target) const {
  auto itr = symtable_address_symbol_map_.find(target);
  if (itr == symtable_address_symbol_map_.end())
    return false;
  return itr->second.Name.find(".resume") != std::string::npos;
}

void SymbolMap::CalculateThresholdFromTotalCount() {
  uint64_t total_count = 0;
  for (const auto &symbol : map_) {
    total_count += symbol.second->total_count;
  };
  count_threshold_ = std::max((uint64_t) (total_count * SampleThresholdFrac),  SampleThresholdAbsl.getValue());
  LLVM_DEBUG(std::cout << " SampleThresholdFrac : " << SampleThresholdFrac
                       << " total count :  " << total_count
                       << "threshold : " << count_threshold_ << std::endl);
}

static void BuildSymbolMapHelper(SymbolGroups &sym_groups,
                                 AddressSymbolMap &sym_map) {
  for (auto &group : sym_groups) {
    for (auto &symb : group) {
      std::pair<AddressSymbolMap::iterator, bool> ret =
          sym_map.insert({symb.StartAddress, symb});
      if (!ret.second) {
        ElfSymbol &prior = ret.first->second;
        if (prior.Name == symb.Name)
          continue;
        std::cerr << "Duplicated symbol address : 0x  " << std::hex
                  << symb.StartAddress.offset << std::dec << " " << prior.Name
                  << " and " << symb.Name << std::endl;
      }
    }
  }
}

void SymbolMap::BuildSymbolMap(const std::string &binary) {
  SymbolLoader::loadSymbolFromDebugTable(binary, symbol_groups_);
  BuildSymbolMapHelper(symbol_groups_, address_symbol_map_);

  SymbolLoader::loadSymbolFromSymbolTable(binary, symtable_symbol_groups_);
  BuildSymbolMapHelper(symtable_symbol_groups_, symtable_address_symbol_map_);
}

void SymbolMap::AddSymbolEntryCount(const InstructionLocation &symb_location,
                                    uint64_t count) {
  auto symbol_it = map_.find(symb_location);
  if (symbol_it != map_.end())
    symbol_it->second->head_count += count;
}

Symbol *SymbolMap::TraverseInlineStack(Symbol &root_symbol,
                                       const DIInliningInfo &src,
                                       uint64_t count) {

  Symbol *symbol = &root_symbol;
  symbol->total_count += count;
  const DILineInfo &info = src.getFrame(src.getNumberOfFrames() - 1);
  if (symbol->info.FileName.empty() && (!info.FileName.empty())) {
    symbol->info.FileName = info.FileName;
    // TODO: dirname is empty
    // symbol->info.dir_name = info.dir_name;
  }

  for (int i = src.getNumberOfFrames() - 1; i > 0; i--) {
    // We do not mutate these objects, but the API would return
    // a copy if we were to use getFrame*
    const auto &callerInfo = src.getFrame(i);
    const auto &calleeInfo = src.getFrame(i - 1);
    auto offset = InstructionSymbolizer::getOffset(callerInfo);

    std::pair<CallsiteMap::iterator, bool> ret =
        symbol->callsites.insert(CallsiteMap::value_type(
            Callsite(offset, calleeInfo.FunctionName), NULL));
    if (ret.second) {
      ret.first->second = new Symbol(calleeInfo);
    }
    symbol = ret.first->second;
    symbol->total_count += count;
  }
  return symbol;
}

void SymbolMap::AddSourceCount(const InstructionLocation &sample_loc,
                               const DIInliningInfo &src, uint64_t count,
                               uint64_t num_inst, Operation op) {
  if (src.getNumberOfFrames() == 0) {
    return;
  }
  Symbol *root_symbol = this->findSymbolFromLocation(sample_loc);
  if (root_symbol == nullptr) {
    return;
  }
  uint32_t offset = InstructionSymbolizer::getOffset(src.getFrame(0));

  Symbol *symbol = TraverseInlineStack(*root_symbol, src, count);
  if (op == MAX) {
    if (count > symbol->pos_counts[offset].count) {
      symbol->pos_counts[offset].count = count;
    }
  } else if (op == SUM) {
    symbol->pos_counts[offset].count += count;
  } else {
    llvm::errs() << "op not supported.";
    exit(-1);
  }
  // We no longer track the number of instruction per line offset
  symbol->pos_counts[offset].num_inst += num_inst;
}

void SymbolMap::AddIndirectCallTarget(const Branch &branch,
                                      const DIInliningInfo &call_info,
                                      uint64_t count) {
  auto target_func = map_.find(branch.target);

  if (call_info.getNumberOfFrames() == 0 or target_func == map_.end()) {
    return;
  }
  Symbol *caller_symbol = this->findSymbolFromLocation(branch.instruction);
  if (caller_symbol == nullptr)
    return;
  auto duplicationFactor =
      call_info.getNumberOfFrames() < 1
          ? 1
          : InstructionSymbolizer::getDuplicationFactor(call_info.getFrame(0));
  Symbol *symbol = TraverseInlineStack(*caller_symbol, call_info, 0);
  symbol->pos_counts[InstructionSymbolizer::getOffset(call_info.getFrame(0))]
      .target_map[
          target_func->second->info.FunctionName.c_str()] =
      count * duplicationFactor;
}

struct CallsiteLessThan {
  bool operator()(const Callsite &c1, const Callsite &c2) const {
    if (c1.first != c2.first)
      return c1.first < c2.first;
    // if ((c1.second == NULL || c2.second == NULL))
    //  return c1.second == NULL;
    return c1.second < c2.second;
  }
};

void Symbol::Dump(int ident) const {
  if (ident == 0) {
    printf("%s total:%" PRIu64 " head:%" PRIu64 "\n",
           info.FunctionName.c_str(), total_count,
           head_count);
  } else {
    printf("%s total:%" PRIu64 "\n", info.FunctionName.c_str(), total_count);
  }
  std::vector<uint32_t> positions;
  for (const auto &pos_count : pos_counts)
    positions.push_back(pos_count.first);
  std::sort(positions.begin(), positions.end());
  for (const auto &pos : positions) {
    PositionCountMap::const_iterator ret = pos_counts.find(pos);
    assert(ret != pos_counts.end());
    PrintSourceLocation(info.StartLine, pos, ident + 2);
    printf("%" PRIu64, ret->second.count);
    TargetCountPairs target_count_pairs;
    GetSortedTargetCountPairs(ret->second.target_map, &target_count_pairs);
    for (const auto &target_count : target_count_pairs) {
      printf("  %s:%" PRIu64, target_count.first.c_str(), target_count.second);
    }
    printf("\n");
  }
  std::vector<Callsite> calls;
  for (const auto &pos_symbol : callsites) {
    calls.push_back(pos_symbol.first);
  }
  std::sort(calls.begin(), calls.end(), CallsiteLessThan());
  for (const auto &callsite : calls) {
    PrintSourceLocation(info.StartLine, callsite.first, ident + 2);
    callsites.find(callsite)->second->Dump(ident + 2);
  }
}

uint64_t Symbol::MaxPosCallsiteCount() const {
  uint64_t max_count = 0;

  for (const auto &pos_count : pos_counts) {
    max_count = std::max(max_count, pos_count.second.count);
  }

  for (const auto &callsite : callsites) {
    max_count = std::max(max_count, callsite.second->MaxPosCallsiteCount());
  }

  return max_count;
}

void SymbolMap::Dump() const {
  std::map<uint64_t, std::set<InstructionLocation>> count_locations_map;
  for (const auto &location_symbol : map_) {
    if (location_symbol.second->total_count > 0) {
      count_locations_map[~location_symbol.second->total_count].insert(
          location_symbol.first);
    }
  }
  for (const auto &count_locations : count_locations_map) {
    for (const auto &loc : count_locations.second) {
      Symbol *symbol = map_.find(loc)->second;
      symbol->Dump(0);
    }
  }
}

// Consts for profile validation
static const int kMinNumSymbols = 10;
static const int kMinTotalCount = 1000000;
static const float kMinNonZeroSrcFrac = 0.8;

bool SymbolMap::Validate() const {
  if (size() < kMinNumSymbols) {
    llvm::errs() << "# of symbols (" << size() << ") too small.";
    return false;
  }
  uint64_t total_count = 0;
  uint64_t num_srcs = 0;
  uint64_t num_srcs_non_zero = 0;
  bool has_inline_stack = false;
  bool has_call = false;
  bool has_discriminator = false;
  std::vector<const Symbol *> symbols;
  for (const auto &name_symbol : map_) {
    total_count += name_symbol.second->total_count;
    symbols.push_back(name_symbol.second);
    if (name_symbol.second->callsites.size() > 0) {
      has_inline_stack = true;
    }
  }
  while (!symbols.empty()) {
    const Symbol *symbol = symbols.back();
    symbols.pop_back();
    for (const auto &pos_count : symbol->pos_counts) {
      if (pos_count.second.target_map.size() > 0) {
        has_call = true;
      }
      num_srcs++;
      if (pos_count.first != 0) {
        num_srcs_non_zero++;
      }
      if ((pos_count.first & 0xffff) != 0) {
        has_discriminator = true;
      }
    }
    for (const auto &pos_callsite : symbol->callsites) {
      symbols.push_back(pos_callsite.second);
    }
  }
  if (total_count < kMinTotalCount) {
    llvm::errs() << "Total count (" << total_count << ") too small.";
    return false;
  }
  if (!has_call) {
    llvm::errs() << "Do not have a single call.";
    return false;
  }
  if (!has_inline_stack) {
    llvm::errs() << "Do not have a single inline stack.";
    return false;
  }
  if (!has_discriminator) {
    llvm::errs() << "Do not have a single discriminator.";
    return false;
  }
  if (num_srcs_non_zero < num_srcs * kMinNonZeroSrcFrac) {
    llvm::errs() << "Do not have enough non-zero src locations."
                 << " NonZero: " << num_srcs_non_zero << " Total: " << num_srcs;
    return false;
  }
  return true;
}
} // namespace autofdo
