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

// Write profile to afdo file.
// facebook T26943842

#include <algorithm>
#include <map>
#include <set>
#include <stdio.h>
#include <string>
#include <utility>
#include <vector>

#include "ProfileWriter.h"
#include "SymbolMap.h"

llvm::cl::opt<bool> DebugDump(
    "debug_dump",
    llvm::cl::desc("If set, emit additional debugging dumps to stderr."),
    llvm::cl::init(false));

namespace autofdo {
using namespace std;

class SourceProfileLengther : public SymbolTraverser {
public:
  explicit SourceProfileLengther(const SymbolMap &symbol_map)
      : length_(0), num_functions_(0) {
    Start(symbol_map);
  }

  int length() { return length_ + num_functions_ * 2; }
  int num_functions() { return num_functions_; }

protected:
  virtual void VisitTopSymbol(const string &name, const Symbol *node) {
    num_functions_++;
  }

  virtual void Visit(const Symbol *node) {
    // func_name, num_pos_counts, num_callsites
    length_ += 3;
    // offset_discr, num_targets, count * 2
    length_ += node->pos_counts.size() * 4;
    // offset_discr
    length_ += node->callsites.size();
    for (const auto &pos_count : node->pos_counts) {
      // type, func_name * 2, count * 2
      length_ += pos_count.second.target_map.size() * 5;
    }
  }

private:
  int length_;
  int num_functions_;
  // DISALLOW_COPY_AND_ASSIGN(SourceProfileLengther);
};

// Debugging support.  ProfileDumper emits a detailed dump of the contents
// of the input profile.
class ProfileDumper : public SymbolTraverser {
public:
  static void Write(const SymbolMap &symbol_map, const StringIndexMap &map) {
    ProfileDumper writer(map);
    writer.Start(symbol_map);
  }

protected:
  void DumpSourceInfo(const DILineInfo &info, int indent) {
    // printf("%*sDirectory name: %s\n", indent, " ", info.dir_name.c_str());
    printf("%*sFile name:      %s\n", indent, " ", info.FileName.c_str());
    printf("%*sFunction name:  %s\n", indent, " ", info.FunctionName.c_str());
    printf("%*sStart line:     %u\n", indent, " ", info.StartLine);
    printf("%*sLine:           %u\n", indent, " ", info.Line);
    printf("%*sDiscriminator:  %u\n", indent, " ", info.Discriminator);
  }

  void PrintSourceLocation(uint32_t start_line, uint32_t offset) {
    if (offset & 0xffff) {
      printf("%u.%u: ", (offset >> 16) + start_line, offset & 0xffff);
    } else {
      printf("%u: ", (offset >> 16) + start_line);
    }
  }

  virtual void Visit(const Symbol *node) {
    printf("Writing symbol: ");
    node->Dump(4);
    printf("\n");
    printf("Source information:\n");
    DumpSourceInfo(node->info, 0);
    printf("\n");
    printf("Total sampled count:            %" PRIu64 "\n",
           static_cast<uint64_t>(node->total_count));
    printf("Total sampled count in head bb: %" PRIu64 "\n",
           static_cast<uint64_t>(node->head_count));
    printf("\n");
    printf("Call sites:\n");
    int i = 0;
    for (const auto &callsite_symbol : node->callsites) {
      Callsite site = callsite_symbol.first;
      Symbol *symbol = callsite_symbol.second;
      printf("  #%d: site\n", i);
      printf("    uint32: %u\n", site.first);
      printf("    const char *: %s\n", site.second.c_str());
      printf("  #%d: symbol: ", i);
      symbol->Dump(0);
      printf("\n");
      i++;
    }

    printf("node->pos_counts.size() = %" PRIu64 "\n",
           static_cast<uint64_t>(node->pos_counts.size()));
    printf("node->callsites.size() = %" PRIu64 "\n",
           static_cast<uint64_t>(node->callsites.size()));
    std::vector<uint32_t> positions;
    for (const auto &pos_count : node->pos_counts)
      positions.push_back(pos_count.first);
    std::sort(positions.begin(), positions.end());
    i = 0;
    for (const auto &pos : positions) {
      PositionCountMap::const_iterator pos_count = node->pos_counts.find(pos);
      assert(pos_count != node->pos_counts.end());
      uint32_t location = pos_count->first;
      ProfileInfo info = pos_count->second;

      printf("#%d: location (line[.discriminator]) = ", i);
      PrintSourceLocation(node->info.StartLine, location);
      printf("\n");
      printf("#%d: profile info execution count = %" PRIu64 "\n", i,
             info.count);
      printf("#%d: profile info number of instructions = %" PRIu64 "\n", i,
             info.num_inst);
      TargetCountPairs target_counts;
      GetSortedTargetCountPairs(info.target_map, &target_counts);
      printf("#%d: profile info target map size = %" PRIu64 "\n", i,
             static_cast<uint64_t>(info.target_map.size()));
      printf("#%d: info.target_map:\n", i);
      for (const auto &target_count : info.target_map) {
        printf("\tGetStringIndex(target_count.first): %d\n",
               GetStringIndex(target_count.first));
        printf("\ttarget_count.second: %" PRIu64 "\n", target_count.second);
      }
      printf("\n");
      i++;
    }
  }

  virtual void VisitTopSymbol(const string &name, const Symbol *node) {
    printf("VisitTopSymbol: %s\n", name.c_str());
    node->Dump(0);
    printf("node->head_count: %" PRIu64 "\n", node->head_count);
    printf("GetStringIndex(%s): %u\n", name.c_str(), GetStringIndex(name));
    printf("\n");
  }

  virtual void VisitCallsite(const Callsite &callsite) {
    printf("VisitCallSite: %s\n", callsite.second.c_str());
    printf("callsite.first: %u\n", callsite.first);
    printf("GetStringIndex(callsite.second): %u\n",
           GetStringIndex(callsite.second));
  }

private:
  explicit ProfileDumper(const StringIndexMap &map) : map_(map) {}

  int GetStringIndex(const string &str) {
    StringIndexMap::const_iterator ret = map_.find(str);
    assert(ret != map_.end());
    return ret->second;
  }

  const StringIndexMap &map_;
};

// Emit a dump of the input profile on stdout.
void ProfileWriter::Dump() {
  StringIndexMap string_index_map;
  StringTableUpdater::Update(*symbol_map_, &string_index_map);
  SourceProfileLengther length(*symbol_map_);
  printf("Length of symbol map: %d\n", length.length() + 1);
  printf("Number of functions:  %d\n", length.num_functions());
  ProfileDumper::Write(*symbol_map_, string_index_map);
}

} // namespace autofdo
