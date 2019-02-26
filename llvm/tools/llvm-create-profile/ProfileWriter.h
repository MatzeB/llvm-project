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

// Class to build AutoFDO profile.
// facebook T26943842

#ifndef AUTOFDO_PROFILE_WRITER_H_
#define AUTOFDO_PROFILE_WRITER_H_

#include "SymbolMap.h"

extern llvm::cl::opt<bool> DebugDump;
namespace autofdo {
using namespace std;
class SymbolMap;

class ProfileWriter {
public:
  explicit ProfileWriter(const SymbolMap *symbol_map/*,
                         const ModuleMap *module_map*/)
      : symbol_map_(symbol_map)/*, module_map_(module_map)*/ {}
  explicit ProfileWriter() : symbol_map_(nullptr) /*,module_map_(nullptr)*/ {}
  virtual ~ProfileWriter() {}

  virtual bool WriteToFile(const string &output_file) = 0;
  void setSymbolMap(const SymbolMap *symbol_map) { symbol_map_ = symbol_map; }
  // void setModuleMap(const ModuleMap *module_map) { module_map_ = module_map;
  // }
  void Dump();

protected:
  const SymbolMap *symbol_map_;
  // const ModuleMap *module_map_;
};

class SymbolTraverser {
public:
  virtual ~SymbolTraverser() {}

protected:
  SymbolTraverser() : level_(0) {}
  virtual void Start(const SymbolMap &symbol_map) {
    for (const auto &name_symbol : symbol_map.map()) {
      if (!symbol_map.ShouldEmit(name_symbol.second->total_count)) {
        continue;
      }
      VisitTopSymbol(name_symbol.second->info.FunctionName, name_symbol.second);
      Traverse(name_symbol.second);
    }
  }
  virtual void VisitTopSymbol(const string &name, const Symbol *node) {}
  virtual void Visit(const Symbol *node) = 0;
  virtual void VisitCallsite(const Callsite &offset) {}
  int level_;

private:
  void Traverse(const Symbol *node) {
    level_++;
    Visit(node);
    for (const auto &callsite_symbol : node->callsites) {
      VisitCallsite(callsite_symbol.first);
      Traverse(callsite_symbol.second);
    }
    level_--;
  }
  // DISALLOW_COPY_AND_ASSIGN(SymbolTraverser);
};

typedef std::map<string, int> StringIndexMap;

class StringTableUpdater : public SymbolTraverser {
public:
  static void Update(const SymbolMap &symbol_map, StringIndexMap *map) {
    StringTableUpdater updater(map);
    updater.Start(symbol_map);
  }

protected:
  void Visit(const Symbol *node) override {
    for (const auto &pos_count : node->pos_counts) {
      for (const auto &name_count : pos_count.second.target_map) {
        (*map_)[name_count.first] = 0;
      }
    }
  }

  void VisitCallsite(const Callsite &callsite) override {
    (*map_)[Symbol::Name(callsite.second)] = 0;
  }

  void VisitTopSymbol(const string &name, const Symbol *node) override {
    (*map_)[Symbol::Name(name.c_str())] = 0;
  }

private:
  explicit StringTableUpdater(StringIndexMap *map) : map_(map) {}
  StringIndexMap *map_;
};

} // namespace autofdo

#endif // AUTOFDO_PROFILE_WRITER_H_
