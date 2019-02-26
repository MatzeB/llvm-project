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

#ifndef AUTOFDO_SAMPLE_READER_H_
#define AUTOFDO_SAMPLE_READER_H_

#include <assert.h>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace autofdo {

/// Represent an offset inside an objectFile
/// The offset are in "symbolizable form" meaning they can
/// directly be used by the symbolizer
struct InstructionLocation {
  InstructionLocation &operator++() {
    offset++;
    return *this;
  }

  uint64_t operator-(const InstructionLocation &rhs) const {
    assert(objectFile == rhs.objectFile);
    assert(offset >= rhs.offset);
    return offset - rhs.offset;
  }

  bool operator<(const InstructionLocation &rhs) const {
    return std::tie(objectFile, offset) < std::tie(rhs.objectFile, rhs.offset);
  }

  bool operator>(const InstructionLocation &rhs) const { return rhs < *this; }

  bool operator<=(const InstructionLocation &rhs) const {
    return !(rhs < *this);
  }

  bool operator>=(const InstructionLocation &rhs) const {
    return !(*this < rhs);
  }

  friend std::ostream &operator<<(std::ostream &os,
                                  const InstructionLocation &location) {
    os << "ObjectFile: " << location.objectFile
       << " offset: " << location.offset;
    return os;
  }

  const std::string &objectFile;
  uint64_t offset;
};

struct Branch {
  friend std::ostream &operator<<(std::ostream &os, const Branch &branch) {
    os << "instruction: " << branch.instruction << " target: " << branch.target;
    return os;
  }

  bool operator<(const Branch &rhs) const {
    if (instruction < rhs.instruction)
      return true;
    if (rhs.instruction < instruction)
      return false;
    return target < rhs.target;
  }

  bool operator>(const Branch &rhs) const { return rhs < *this; }

  bool operator<=(const Branch &rhs) const { return !(rhs < *this); }

  bool operator>=(const Branch &rhs) const { return !(*this < rhs); }

  InstructionLocation instruction;
  InstructionLocation target;
};

struct Range {
  friend std::ostream &operator<<(std::ostream &os, const Range &range) {
    os << "begin: " << range.begin << " end: " << range.end
       << " length: " << range.end.offset - range.begin.offset;
    return os;
  }
  bool operator<(const Range &rhs) const {
    if (begin < rhs.begin)
      return true;
    if (rhs.begin < begin)
      return false;
    return end < rhs.end;
  }

  bool operator>(const Range &rhs) const { return rhs < *this; }

  bool operator<=(const Range &rhs) const { return !(rhs < *this); }

  bool operator>=(const Range &rhs) const { return !(*this < rhs); }

  InstructionLocation begin;
  InstructionLocation end;
};
using namespace std;

typedef map<InstructionLocation, uint64_t> AddressCountMap;
typedef map<Range, uint64_t> RangeCountMap;
typedef map<Branch, uint64_t> BranchCountMap;

class AbstractSampleReader {
public:
  virtual ~AbstractSampleReader(){};
  virtual const AddressCountMap &address_count_map() const = 0;
  virtual const RangeCountMap &range_count_map() const = 0;
  virtual const BranchCountMap &branch_count_map() const = 0;
  virtual bool readProfile() = 0;
  // virtual bool ReadAndSetTotalCount();
  // Returns the total sampled count.
  virtual uint64_t GetTotalSampleCount() const = 0;
  // Returns the max count.
  virtual uint64_t GetTotalCount() const = 0;
};

// Reads/Writes sample data from/to text file.
// The text file format:
//
// number of entries in rangeCountMap
// from_1-to_1:count_1
// from_2-to_2:count_2
// ......
// from_n-to_n:count_n
// number of entries in address_count_map
// addr_1:count_1
// addr_2:count_2
// ......
// addr_n:count_n
class TextSampleReaderWriter : public AbstractSampleReader {
public:
  explicit TextSampleReaderWriter(const string &profile_file,
                                  const std::string objectFile)
      : objectFile(objectFile), profileFile(profile_file){};
  virtual bool readProfile() override;

  const AddressCountMap &address_count_map() const override {
    return address_count_map_;
  }

  const RangeCountMap &range_count_map() const override {
    return range_count_map_;
  }

  const BranchCountMap &branch_count_map() const override {
    return branch_count_map_;
  }

  // Returns the total sampled count.
  uint64_t GetTotalSampleCount() const override;
  // Returns the max count.
  uint64_t GetTotalCount() const override { return total_count_; }

private:
  void Dump(std::ostream &out);
  uint64_t total_count_;
  std::string objectFile;
  std::string profileFile;
  AddressCountMap address_count_map_;
  RangeCountMap range_count_map_;
  BranchCountMap branch_count_map_;
};

} // namespace autofdo

#endif // AUTOFDO_SAMPLE_READER_H_
