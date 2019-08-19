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

// Create AutoFDO Profile.
// facebook T26943842

#include <memory>

#include "InstructionSymbolizer.h"
#include "Profile.h"
#include "ProfileCreator.h"
#include "ProfileWriter.h"
#include "SampleReader.h"
#include "SymbolMap.h"

namespace autofdo {
using namespace std;


cl::opt<bool> AddEmptyRanges ("fill-missing-addresses-with-zero",
                              cl::desc("When an address in the binary is not sampled, attributes to it a count of zero"),
                              cl::init(true));

cl::opt<bool> AddEmptyBinary ("fill-missing-binaries-with-zero",
                              cl::desc("When a binary doesn't have sample hits, attributes to all address in it a count of zero"),
                              cl::init(false));

bool ProfileCreator::CreateProfile(const string &input_profile_name,
                                   const string &profiler,
                                   ProfileWriter *writer,
                                   const string &output_profile_name) {
  if (!ReadSample(input_profile_name, profiler)) {
    return false;
  }
  if (!CreateProfileFromSample(writer, output_profile_name)) {
    return false;
  }
  return true;
}

bool ProfileCreator::ReadSample(const string &input_profile_name,
                                const string &profiler) {
  if (profiler == "perf") {
    sample_reader_ =
        new PerfDataSampleReader(input_profile_name, this->object_files_);
    llvm::errs() << " Perf profiler still in beta \n";

  } else if (profiler == "text") {
    sample_reader_ =
        new TextSampleReaderWriter(input_profile_name, this->object_files_[0]);
  } else {
    llvm::errs() << "Unsupported profiler type: " << profiler;
    return false;
  }
  if (!sample_reader_->readProfile()) {
    llvm::errs() << "Error reading profile.";
    return false;
  }
  return true;
}

bool ProfileCreator::ComputeProfile(SymbolMap &symbol_map) {
  Profile profile(symbol_map);
  profile.ComputeProfile(sample_reader_->range_count_map(),
                         sample_reader_->address_count_map(),
                         sample_reader_->branch_count_map());
  if (AddEmptyRanges) {
     for (const auto &p : symbol_map.map()) {
         auto &instruction = p.first ;
         auto &ProfileSymbol = p.second;
         if (!symbol_map.ShouldEmit(ProfileSymbol->total_count))
           continue;
         auto &elfSymbol = symbol_map.ElfSymbols().at(instruction);
         profile.AddRange(elfSymbol.range(), 0);
      }
  }
  return true;
}

bool ProfileCreator::CreateProfileFromSample(ProfileWriter *writer,
                                             const string &output_name) {

  std::set<std::string> binaries;
  for (const auto &addr : sample_reader_->address_count_map()) {
    binaries.insert(addr.first.objectFile);
  }

  for (const auto &branch : sample_reader_->branch_count_map()) {
    binaries.insert(branch.first.instruction.objectFile);
    binaries.insert(branch.first.target.objectFile);
  }

  for (const auto &range : sample_reader_->range_count_map()) {
    binaries.insert(range.first.begin.objectFile);
    binaries.insert(range.first.end.objectFile);
  }

  if (AddEmptyBinary) {
    for (auto &binary : object_files_)
      binaries.insert(binary);
  }

  SymbolMap symbol_map(std::move(binaries));
  symbol_map.set_use_discriminator_encoding(use_discriminator_encoding_);
  if (!ComputeProfile(symbol_map))
    return false;

  writer->setSymbolMap(&symbol_map);
  bool ret = writer->WriteToFile(output_name);
  return ret;
}

uint64_t ProfileCreator::TotalSamples() {
  if (sample_reader_ == nullptr) {
    return 0;
  } else {
    return sample_reader_->GetTotalSampleCount();
  }
}

} // namespace autofdo
