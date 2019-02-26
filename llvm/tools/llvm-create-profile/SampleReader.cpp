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

// Read the samples from the profile datafile.
// facebook T26943842

#include "SampleReader.h"
#include "SymbolLoader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <inttypes.h>
#include <string>
#include <utility>
#define DEBUG_TYPE "autofdo_text_sample"

namespace autofdo {

uint64_t TextSampleReaderWriter::GetTotalSampleCount() const {
  uint64_t ret = 0;

  if (range_count_map_.size() > 0) {
    for (const auto &range_count : range_count_map_) {
      ret += range_count.second;
    }
  } else {
    for (const auto &addr_count : address_count_map_) {
      ret += addr_count.second;
    }
  }
  return ret;
}

bool TextSampleReaderWriter::readProfile() {
  uint64_t base = SymbolLoader::preferedBasedAddress(objectFile);

  FILE *fp = fopen(profileFile.c_str(), "r");
  if (fp == NULL) {
    llvm::errs() << "Cannot open " << profileFile << "to read";
    return false;
  }
  uint64_t num_records;

  // Reads in the rangeCountMap
  if (1 != fscanf(fp, "%" PRIu64 "\n", &num_records)) {
    llvm::errs() << "Error reading from " << profileFile;
    fclose(fp);
    return false;
  }
  for (uint64_t i = 0; i < num_records; i++) {
    uint64_t from, to, count;
    if (3 != fscanf(fp, "%" PRIx64 "-%" PRIx64 ":%" PRIu64 "\n", &from, &to, &count)) {
      llvm::errs() << "Error reading from " << profileFile;
      fclose(fp);
      return false;
    }

    range_count_map_[Range{InstructionLocation{objectFile, base + from},
                           InstructionLocation{objectFile, base + to + 1}}] +=
        count;
  }

  // Reads in the addr_count_map
  if (1 != fscanf(fp, "%" PRIu64 "\n", &num_records)) {
    llvm::errs() << "Error reading from " << profileFile;
    fclose(fp);
    return false;
  }
  for (uint64_t i = 0; i < num_records; i++) {
    uint64_t addr, count;
    if (2 != fscanf(fp, "%" PRIx64 ":%" PRIu64 "\n", &addr, &count)) {
      llvm::errs() << "Error reading from " << profileFile;
      fclose(fp);
      return false;
    }
    address_count_map_[InstructionLocation{objectFile, base + addr}] += count;
  }

  // Reads in the branchCountMap
  if (1 != fscanf(fp, "%" PRIu64 "\n", &num_records)) {
    llvm::errs() << "Error reading from " << profileFile;
    fclose(fp);
    return false;
  }
  for (uint64_t i = 0; i < num_records; i++) {
    uint64_t from, to, count;
    if (3 != fscanf(fp, "%" PRIx64 "->%" PRIx64 ":%" PRIu64 "\n", &from, &to, &count)) {
      llvm::errs() << "Error reading from " << profileFile;
      fclose(fp);
      return false;
    }
    branch_count_map_[Branch{InstructionLocation{objectFile, base + from},
                             InstructionLocation{objectFile, base + to}}] +=
        count;
  }
  fclose(fp);

  total_count_ = 0;
  if (range_count_map_.size() > 0) {
    for (const auto &range_count : range_count_map_) {
      total_count_ += range_count.second * (range_count.first.end.offset -
                                            range_count.first.begin.offset);
    }
  } else {
    for (const auto &addr_count : address_count_map_) {
      total_count_ += addr_count.second;
    }
  }

  LLVM_DEBUG(Dump(std::cout));
  return true;
}

void TextSampleReaderWriter::Dump(std::ostream &out) {
  out << "Address count "
      << "\n";
  for (auto const &addr : address_count_map())
    out << std::hex << addr.first << std::dec << " : " << addr.second
        << std::endl;

  out << "Range count " << std::endl;
  for (auto const &range : range_count_map())
    out << std::hex << range.first << std::dec << " : " << range.second
        << std::endl;

  out << "Branch count " << std::endl;
  for (auto const &branch : branch_count_map())
    out << std::hex << branch.first << std::dec << " : " << branch.second
        << std::endl;
}

} // namespace autofdo
