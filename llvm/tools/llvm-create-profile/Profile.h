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

// Class to extract function level profile from binary level samples.
// facebook T26943842

#ifndef AUTOFDO_PROFILE_H_
#define AUTOFDO_PROFILE_H_

#include <map>
#include <set>
#include <string>

#include "InstructionSymbolizer.h"
#include "SampleReader.h"

namespace autofdo {
using namespace std;
class SymbolMap;

// Class to convert instruction level profile to source level profile.
class Profile {
public:
  // Arguments:
  //   symbol_map: the symbol map is written by this class to store all symbol
  //               information.
  Profile(SymbolMap &symbol_map) : symbol_map_(symbol_map) {}

  ~Profile();

  // Builds the source level profile.
  void ComputeProfile(const RangeCountMap &range_map,
                      const AddressCountMap &address_map,
                      const BranchCountMap &branch_map);

  void AddRange(const Range &range, uint64_t count) ;
  void AddInstruction(const InstructionLocation &inst, uint64_t count);

private:
  InstructionSymbolizer symbolizer;
  SymbolMap &symbol_map_;

  // DISALLOW_COPY_AND_ASSIGN(Profile);
};
} // namespace autofdo

#endif // AUTOFDO_PROFILE_H_
