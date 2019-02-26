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

// Convert a Perf profile to LLVM.
// facebook T26943842

#include "LLVMProfileWriter.h"
#include "ProfileWriter.h"
#include <algorithm>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <map>
#include <set>
#include <stdio.h>
#include <string>
#include <utility>
#include <vector>

// bool debug_dump = false ;

namespace autofdo {
using namespace std;
bool LLVMProfileBuilder::Write(const string &output_filename,
                               llvm::sampleprof::SampleProfileFormat format,
                               const SymbolMap &symbol_map,
                               const StringIndexMap &name_table) {
  // Collect the profiles for every symbol in the name table.
  LLVMProfileBuilder builder(name_table);
  const auto &profiles = builder.ConvertProfiles(symbol_map);

  // Write all the gathered profiles to the output file.
  auto WriterOrErr = llvm::sampleprof::SampleProfileWriter::create(
      llvm::StringRef(output_filename), format);
  if (std::error_code EC = WriterOrErr.getError()) {
    llvm::errs() << "Error creating profile output file '" << output_filename
                 << "': " << EC.message();
    return false;
  }
  auto Writer = std::move(WriterOrErr.get());
  if (std::error_code EC = Writer->write(profiles)) {
    llvm::errs() << "Error writing profile output to '" << output_filename
                 << "': " << EC.message();
    return false;
  }

  return true;
}

const SampleProfileMap &
LLVMProfileBuilder::ConvertProfiles(const SymbolMap &symbol_map) {
  Start(symbol_map);
  return GetProfiles();
}

void LLVMProfileBuilder::VisitTopSymbol(const string &name,
                                        const Symbol *node) {
  llvm::StringRef name_ref = GetNameRef(name);
  llvm::sampleprof::FunctionSamples &profile = profiles_[name_ref];
  if (std::error_code EC = llvm::MergeResult(
          result_, profile.addHeadSamples(node->head_count))) {
    llvm::errs() << "Error updating head samples for '" << name
                 << "': " << EC.message();
    // llvm::ExitOnError(llvm::Error<void>);
    exit(-1);
  }
  if (std::error_code EC = llvm::MergeResult(
          result_, profile.addTotalSamples(node->total_count))) {
    llvm::errs() << "Error updating total samples for '" << name
                 << "': " << EC.message();
    exit(-1);
  }
  profile.setName(name_ref);
  inline_stack_.clear();
  inline_stack_.push_back(&profile);
}

void LLVMProfileBuilder::VisitCallsite(const Callsite &callsite) {
  assert(inline_stack_.size() >= 1);
  uint32_t offset = callsite.first;
  uint32_t line = offset >> 16;
  uint32_t discriminator = offset & 0xffff;
  while (inline_stack_.size() > static_cast<size_t>(level_)) {
    inline_stack_.pop_back();
  }
  auto &caller_profile = *(inline_stack_.back());
  auto CalleeName = GetNameRef(Symbol::Name(callsite.second));
  auto &callee_profile =
      caller_profile.functionSamplesAt(llvm::sampleprof::LineLocation(
          line, discriminator))[std::string(CalleeName)];
  callee_profile.setName(CalleeName);
  inline_stack_.push_back(&callee_profile);
}

void LLVMProfileBuilder::Visit(const Symbol *node) {
  assert(inline_stack_.size() >= 1);
  auto &profile = *(inline_stack_.back());

  if (level_ > 1) {
    // If this is a nested inline call, update its total count.
    if (std::error_code EC = llvm::MergeResult(
            result_, profile.addTotalSamples(node->total_count))) {
      llvm::errs() << "Error updating total samples for '"
                   << node->info.FunctionName << "': " << EC.message();
      exit(-1);
    }
  }

  // Emit all the locations and their sample counts.
  for (const auto &pos_count : node->pos_counts) {
    uint32_t offset = pos_count.first;
    uint32_t line = offset >> 16;
    uint32_t discriminator = offset & 0xffff;
    const auto &num_samples = pos_count.second.count;
    if (std::error_code EC = llvm::MergeResult(
            result_,
            profile.addBodySamples(line, discriminator, num_samples))) {
      llvm::errs() << "Error updating body samples for '"
                   << node->info.FunctionName << "': " << EC.message();
    }
    // If there is a call at this location, emit the possible
    // targets. For direct calls, this will be the exact function
    // being invoked. For indirect calls, this will be a list of one
    // or more functions.
    const auto &target_map = pos_count.second.target_map;
    for (const auto &target_count : target_map) {
      if (std::error_code EC = llvm::MergeResult(
              result_, profile.addCalledTargetSamples(line, discriminator,
                                                      target_count.first,
                                                      target_count.second))) {
        llvm::errs() << "Error updating called target samples for '"
                     << node->info.FunctionName << "': " << EC.message();
      }
    }
  }
}

llvm::StringRef LLVMProfileBuilder::GetNameRef(const string &str) {
  StringIndexMap::const_iterator ret =
      name_table_.find(Symbol::Name(str.c_str()));
  assert(ret != name_table_.end());
  return llvm::StringRef(ret->first.c_str());
}

bool LLVMProfileWriter::WriteToFile(const string &output_filename) {
  if (DebugDump)
    Dump();

  // Populate the symbol table. This table contains all the symbols
  // for functions found in the binary.
  StringIndexMap name_table;
  StringTableUpdater::Update(*symbol_map_, &name_table);

  // Gather profiles for all the symbols.
  return LLVMProfileBuilder::Write(output_filename, format_, *symbol_map_,
                                   name_table);
}

} // namespace autofdo
