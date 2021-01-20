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

// Class to represent the symbol map. The symbol map is a map from
// symbol names to the symbol class.
// This class is thread-safe.
// facebook T26943842
#ifndef AUTOFDO_SYMBOL_MAP_H_
#define AUTOFDO_SYMBOL_MAP_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "InstructionSymbolizer.h"
#include "PerfSampleReader.h"
#include "SymbolLoader.h"

#define DEBUG_TYPE "autofdo_symbol_map"

extern llvm::cl::opt<std::string> PrintSymbolList;

namespace autofdo {
using namespace std;
typedef std::map<std::string, uint64_t> CallTargetCountMap;
typedef std::pair<std::string, uint64_t> TargetCountPair;
typedef std::vector<TargetCountPair> TargetCountPairs;

// Returns a sorted vector of target_count pairs. target_counts is a pointer
// to an empty vector in which the output will be stored.
// Sorting is based on count in descending order.
void GetSortedTargetCountPairs(const CallTargetCountMap &call_target_count_map,
                               TargetCountPairs *target_counts);

// Represents profile information of a given source.
class ProfileInfo {
public:
  ProfileInfo() : count(0), num_inst(0) {}
  ProfileInfo &operator+=(const ProfileInfo &other);

  uint64_t count;
  // Unused: Compare the to the original tool in github/autofdo we no longer
  // track
  // the number of instructions per line
  uint64_t num_inst;
  CallTargetCountMap target_map;
};

// Map from a source location (represented by offset+discriminator) to profile.
typedef std::map<uint32_t, ProfileInfo> PositionCountMap;

// callsite_location, callee_name
typedef std::pair<uint32_t, std::string> Callsite;

struct CallsiteLess {
  bool operator()(const Callsite &c1, const Callsite &c2) const {
    if (c1.first != c2.first)
      return c1.first < c2.first;
    // if ((c1.second == NULL || c2.second == NULL))
    //  return c1.second < c2.second;
    return c1.second < c2.second;
  }
};
class Symbol;
// Map from a callsite to the callee symbol.
typedef std::map<Callsite, Symbol *, CallsiteLess> CallsiteMap;

// Contains information about a specific symbol.
// There are two types of symbols:
// 1. Actual symbol: the symbol exists in the binary as a standalone function.
//                   It has the begin_address and end_address, and its name
//                   is always full assembler name.
// 2. Inlined symbol: the symbol is cloned in another function. It does not
//                    have the begin_address and end_address, and its name
//                    could be a short bfd_name.
class Symbol {
public:
  // This constructor is used to create inlined symbol.
  Symbol(const DILineInfo &info) : info(info), total_count(0), head_count(0) {}

  // This constructor is used to create aliased symbol.
  Symbol(const Symbol *src, const char *new_func_name)
      : info(src->info), total_count(src->total_count),
        head_count(src->head_count) {
    info.FunctionName = std::string(new_func_name);
  }

  Symbol() : total_count(0), head_count(0) {}

  ~Symbol();

  static string Name(const char *name) {
    return (name && strlen(name) > 0) ? name : "noname";
  }

  static string Name(std::string name) { return name != "" ? name : "noname"; }
  string name() const { return Name(info.FunctionName.c_str()); }

  // Merges profile stored in src symbol with this symbol.
  void Merge(const Symbol *src);

  // Returns the module name of the symbol. Module name is the source file
  // that the symbol belongs to. It is an attribute of the actual symbol.
  string ModuleName() const;

  // Returns true if the symbol is from a header file.
  bool IsFromHeader() const;

  // Dumps content of the symbol with a give indentation.
  void Dump(int indent) const;

  // Returns the max of pos_counts and callsites' pos_counts.
  uint64_t MaxPosCallsiteCount() const;

  // Source information about the the symbol (func_name, file_name, etc.)
  DILineInfo info;
  // The total sampled count.
  uint64_t total_count;
  // The total sampled count in the head bb.
  uint64_t head_count;
  // Map from callsite location to callee symbol.
  CallsiteMap callsites;
  // Map from source location to count and instruction number.
  PositionCountMap pos_counts;
};

using ElfSymbol = SymbolLoader::ElfSymbol;
// Maps function location to actual symbol. (Top level map).
typedef map<InstructionLocation, Symbol *> LocationSymbolMap;
// Maps symbol's start address to its name and size.
typedef std::map<InstructionLocation, ElfSymbol &> AddressSymbolMap;
// Maps from symbol's name to its start address.
typedef std::map<string, InstructionLocation> NameAddressMap;
// Maps function name to alias names.
typedef map<InstructionLocation, set<string>> NameAliasMap;
// Maps function name to split ranges.
typedef vector<vector<ElfSymbol>> SymbolGroups;

// SymbolMap stores the symbols in the binary, and maintains
// a map from symbol name to its related information.
class SymbolMap {
public:
  explicit SymbolMap(const std::set<std::string> &binaries)
      : binaries_(binaries), count_threshold_(0),
        use_discriminator_encoding_(false) {

    for (const auto &binary : binaries_) {
      BuildSymbolMap(binary);
    }

    for (const auto &s : this->address_symbol_map_) {
      this->AddSymbol(s.second.StartAddress, s.second.Name);
    }

    if (PrintSymbolList.getNumOccurrences() > 0) {
      std::ofstream out(PrintSymbolList);
      dumpaddressmap(out);
    }
  }

  SymbolMap() : count_threshold_(0), use_discriminator_encoding_(false) {}

  ~SymbolMap();

  uint64_t size() const { return map_.size(); }

  void set_count_threshold(double n) { count_threshold_ = n; }
  int64_t count_threshold() const { return count_threshold_; }

  // Returns true if the count is large enough to be emitted.
  bool ShouldEmit(uint64_t count) const { return count >= count_threshold_; }

  // Caculates sample threshold from given total count.
  void CalculateThresholdFromTotalCount();

  void set_use_discriminator_encoding(bool v) {
    use_discriminator_encoding_ = v;
  }

  // Adds an empty named symbol.
  void AddSymbol(const InstructionLocation &loc, const std::string &name);

  // Rmoves a symbol starting at loc.
  void RemoveSymbol(const InstructionLocation &loc);

  // Finds a symbol starting at loc.
  Symbol *FindSymbol(const InstructionLocation &loc) const;

  const LocationSymbolMap &map() const { return map_; }

  LocationSymbolMap &map() { return map_; }

  const SymbolGroups &getSymbolGroups() const { return symbol_groups_; }

  // Merges symbols with the same name.
  void MergeSplitFunctions();

  // Increments symbol's entry count.
  void AddSymbolEntryCount(const InstructionLocation &symbol_location,
                           uint64_t count);

  typedef enum { INVALID = 1, SUM, MAX } Operation;
  // Increments source stack's count.
  //   source: source location (in terms of inlined source stack).
  //   count: total sampled count.
  //   num_inst: number of instructions that is mapped to the source.
  //   op: operation used to calculate count (SUM or MAX).
  void AddSourceCount(const InstructionLocation &sample_location,
                      const DIInliningInfo &source, uint64_t count,
                      uint64_t num_inst, Operation op);

  // Adds the indirect call target to source stack.
  //   symbol: name of the symbol in which source is located.
  //   source: source location (in terms of inlined source stack).
  //   target: indirect call target.
  //   count: total sampled count.
  void AddIndirectCallTarget(const Branch &call, const DIInliningInfo &source,
                             uint64_t count);

  // Traverses the inline stack in source, update the symbol map by adding
  // count to the total count in the inlined symbol. Returns the leaf symbol.
  Symbol *TraverseInlineStack(Symbol &root_symbol, const DIInliningInfo &source,
                              uint64_t count);

  void Dump() const;

  // Validates if the current symbol map is sane.
  bool Validate() const;

public:
  // Reads from the binary's elf section to build the symbol map.
  void BuildSymbolMap(const std::string &binary);

  Symbol *findSymbolFromLocation(const InstructionLocation &loc) {
    assert(address_symbol_map_.size() > 0);
    auto symb_it = address_symbol_map_.upper_bound(loc);

    if (symb_it == address_symbol_map_.begin()) {
      // The location is lower than all the start addresses
      return nullptr;
    }

    symb_it--;
    if (symb_it->first.objectFile != loc.objectFile) {
      // All the functions in objetFile start at an address > loc.offset.
      return nullptr;
    }

    if (symb_it->first.offset + symb_it->second.Size >= loc.offset) {
      assert(map_.count(symb_it->first) == 1);
      LLVM_DEBUG(
          std::cout << "loc : " << std::hex << loc << " <--> "
                    << symb_it->second.Name << " : " << symb_it->first << " : "
                    << symb_it->first.offset + symb_it->second.Size << " : "
                    << map_[symb_it->first]->name() << std::dec << std::endl);
      return map_[symb_it->first];
    };

    return nullptr;
  }

  void dumpaddressmap(std::ofstream &out) {
    for (auto &addr : address_symbol_map_) {
      out << "name : " << addr.second.Name;
      out << std::hex << ", offset : " << addr.first << std::dec
          << ", size : " << addr.second.Size << std::endl;
    }
  }

  const AddressSymbolMap &ElfSymbols() { return address_symbol_map_; }

  // Validates if the branch is a cross-function branch.
  bool IsCrossFunctionBranch(const Branch &branch) const;

  /// Check whether the call target \p B is a coroutine resume function,
  /// i.e. a ".resume" function generated by splitting a coroutine.
  bool IsCallToCoroutineResume(const InstructionLocation &target) const;

private:
  LocationSymbolMap map_;
  // Symbol groups and map read from dwarf. This is what we use by default.
  SymbolGroups symbol_groups_;
  AddressSymbolMap address_symbol_map_;
  // Symbol groups and map read from symbole table. We cross-check this for
  // special cases such as coroutines.
  SymbolGroups symtable_symbol_groups_;
  AddressSymbolMap symtable_address_symbol_map_;
  std::set<string> binaries_;
  double count_threshold_;
  bool use_discriminator_encoding_;
};
} // namespace autofdo
#undef DEBUG_TYPE
#endif // AUTOFDO_SYMBOL_MAP_H_
