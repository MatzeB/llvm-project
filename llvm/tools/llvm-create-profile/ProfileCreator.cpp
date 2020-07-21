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

#include <float.h>
#include <memory>

#include "InstructionSymbolizer.h"
#include "Profile.h"
#include "ProfileCreator.h"
#include "ProfileWriter.h"
#include "SampleReader.h"
#include "SymbolMap.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_os_ostream.h"

extern llvm::cl::opt<double> HotFunctionCutoff;
extern llvm::cl::opt<double> HotFunctionDensityThreshold;
extern llvm::cl::opt<bool> ShowDensity;
extern llvm::cl::opt<bool> ListHotFunction;
extern const double DefaultHotFunctionCutoff;
extern const double DefaultHotFunctionDensityThreshold;

namespace autofdo {
using namespace std;


cl::opt<bool> AddEmptyRanges ("fill-missing-addresses-with-zero",
                              cl::desc("When an address in the binary is not sampled, attributes to it a count of zero"),
                              cl::init(true));

cl::opt<bool> AddEmptyBinary ("fill-missing-binaries-with-zero",
                              cl::desc("When a binary doesn't have sample hits, attributes to all address in it a count of zero"),
                              cl::init(false));

struct FuncInfo {
  size_t FuncSize;
  uint64_t TotalSample;
  uint64_t EntrySample;
  double FuncDensity;
  bool IsHot;

  FuncInfo()
      : FuncSize(0), TotalSample(0), EntrySample(0), FuncDensity(0.0),
        IsHot(false) {}

  FuncInfo(size_t FuncSize, uint64_t TotalSample, uint64_t EntrySample)
      : FuncSize(FuncSize), TotalSample(TotalSample), EntrySample(EntrySample),
        FuncDensity(0.0), IsHot(false) {}
};

// FuncStatMap loads function statistics such as total samples and function
// sizes from a SymbolMap object into maps for fast retrieval. A FuncStatMap
// object must be initialized with a SymbolMap object and initialized after
// SymbolMap::MergeSplitFunctions() is called.
struct FuncStatMap {
  // Map of function name and their total samples in descending order of total
  // samples.
  std::multimap<uint64_t, std::string, std::greater<uint64_t>>
      FunctionTotalSampleMap;
  // Map of function name to the function size in bytes.
  StringMap<FuncInfo> FunctionInfoMap;
  // Sum of total samples of all functions in the profile.
  uint64_t ProfileTotalSample;
  // Sum of total samples of all hot functions in the profile.
  uint64_t HotFuncTotalSample;
  // Number of hot functions.
  uint64_t HotFuncCount;

  FuncStatMap() = delete;

  // Load function size and total samples from SymbMap to
  // FunctionTotalSampleMap and FunctionInfoMap. It also calculates
  // ProfileTotalSample as the total samples of the profile. Note that
  // HotFuncTotalSample and HotFuncCount are initialize to 0 after construction
  // and need additional computation.
  FuncStatMap(const SymbolMap &SymbMap);
};

FuncStatMap::FuncStatMap(const SymbolMap &SymbMap) {
  ProfileTotalSample = 0;
  HotFuncTotalSample = 0;
  HotFuncCount = 0;
  for (const auto &G : SymbMap.getSymbolGroups()) {
    for (const auto &S : G) {
      Symbol *SymbProfile = SymbMap.FindSymbol(S.StartAddress);
      if (SymbProfile && !FunctionInfoMap.count(S.Name)) {
        FunctionTotalSampleMap.emplace(SymbProfile->total_count, S.Name);
        FunctionInfoMap.try_emplace(StringRef(S.Name),
                                    FuncInfo(S.MergedSize, SymbProfile->total_count,
                                             SymbProfile->head_count));
        ProfileTotalSample += SymbProfile->total_count;
      }
    }
  }
}

// This function calculates function densities of hot functions specified by
// HotFunctionCutoff. For hot functions, the FuncInfo::FuncDensity field in
// FuncStat.FunctionInfoMap is updated with the computed density and the
// FuncInfo::IsHot field is marked true after the call. This function also
// calculates FuncStat.HotFuncTotalSample and FuncStat.HotFuncCount.
static double calculateHotFunctionStatAndDensity(FuncStatMap &FuncStat) {
  // Profile density of functions will be computed until the total samples of
  // functions processed accumulate to TotalSampleThreshold.
  uint64_t TotalSampleThreshold =
      std::ceil(HotFunctionCutoff / 100 * FuncStat.ProfileTotalSample);
  assert(TotalSampleThreshold > 0 &&
         "TotalSampleThreshold should be positive because HotFunctionCutoff "
         "and FuncStat.ProfileTotalSample should both be positive");

  double ProfileDensity = DBL_MAX;
  for (const auto &FuncSample : FuncStat.FunctionTotalSampleMap) {
    if (FuncStat.HotFuncTotalSample >= TotalSampleThreshold)
      break;

    assert(FuncStat.FunctionInfoMap.count(FuncSample.second) &&
           "Information of functions in FuncStat.FunctionTotalSampleMap should "
           "have been loaded in FuncStat.FunctionInfoMap");
    size_t FuncSize = FuncStat.FunctionInfoMap[FuncSample.second].FuncSize;
    assert(FuncSize > 0 && "Function size should not be zero");
    double FuncDensity = static_cast<double>(FuncSample.first) / FuncSize;
    FuncStat.FunctionInfoMap[FuncSample.second].FuncDensity = FuncDensity;
    ProfileDensity = std::min(ProfileDensity, FuncDensity);

    FuncStat.FunctionInfoMap[FuncSample.second].IsHot = true;
    ++FuncStat.HotFuncCount;
    FuncStat.HotFuncTotalSample += FuncSample.first;
  }
  return ProfileDensity;
}

// This function should be called after calculateHotFunctionStatAndDensity() so
// that the profile density is already known.
static void printDensitySuggestion(double Density, raw_os_ostream &OS) {
  assert(Density > 0.0 &&
         "Profile density should be non-zero because all hot functions should "
         "have non-zero total samples and thus non-zero densities");
  assert(HotFunctionDensityThreshold > 0.0 &&
         "Zero and negative density threshold should be ignored and replaced "
         "with default value");

  if (Density < HotFunctionDensityThreshold)
    OS << "AutoFDO is estimated to optimize better with "
       << format("%.2f", HotFunctionDensityThreshold / Density)
       << "x more samples. Please consider increasing sampling rate or "
          "profiling for longer duration to get more samples.\n";

  if (ShowDensity)
    OS << "Minimum profile density for hot functions with top "
       << format("%.2f", HotFunctionCutoff.getValue())
       << "% total samples: " << format("%.2f", Density) << "\n";
}

// This function should be called after calculateHotFunctionStatAndDensity() so
// that statistics such as function densities, the number of hot functions, and
// their total samples are already known.
static void printHotFunctionList(FuncStatMap &FuncStat, raw_os_ostream &OS) {
  assert(FuncStat.HotFuncCount > 0 &&
         "There should be at least one hot function because HotFunctionCutoff "
         "should be non-zero");
  assert(FuncStat.ProfileTotalSample > 0 && "The profile should not be empty");
  double HotFuncCountPercent = static_cast<double>(FuncStat.HotFuncCount) /
                               FuncStat.FunctionInfoMap.size() * 100;
  double HotFuncSamplePercent =
      static_cast<double>(FuncStat.HotFuncTotalSample) /
      FuncStat.ProfileTotalSample * 100;

  formatted_raw_ostream FOS(OS);
  FOS << FuncStat.HotFuncCount << " out of " << FuncStat.FunctionInfoMap.size()
      << " functions (" << format("%.2f%%", HotFuncCountPercent)
      << ") are considered hot functions.\n";
  FOS << FuncStat.HotFuncTotalSample << " out of "
      << FuncStat.ProfileTotalSample << " samples ("
      << format("%.2f%%", HotFuncSamplePercent)
      << ") are from hot functions.\n";

  // Width taken by each column.
  const int TotalSampleWidth = 20;
  const int EntrySampleWidth = 15;
  const int FuncSizeWidth = 17;
  const int DensityWidth = 17;
  // Keeps the current column offset from beginning of a line.
  int ColumnOffset = 0;

  FOS << "Total sample (%)";
  ColumnOffset += TotalSampleWidth;

  FOS.PadToColumn(ColumnOffset);
  FOS << "Entry sample";
  ColumnOffset += EntrySampleWidth;

  FOS.PadToColumn(ColumnOffset);
  FOS << "Function Size";
  ColumnOffset += FuncSizeWidth;

  FOS.PadToColumn(ColumnOffset);
  if (ShowDensity) {
    FOS << "Sample density";
    ColumnOffset += DensityWidth;
  }

  FOS.PadToColumn(ColumnOffset);
  FOS << "Function Name\n";

  for (const auto &FuncSample : FuncStat.FunctionTotalSampleMap) {
    assert(FuncStat.FunctionInfoMap.count(FuncSample.second) &&
           "Information of functions in FuncStat.FunctionTotalSampleMap should "
           "have been loaded in FuncStat.FunctionInfoMap");
    const auto &Func = FuncStat.FunctionInfoMap[FuncSample.second];
    // The IsHot field is initialized to false and marked true only for hot
    // functions in calculateHotFunctionStatAndDensity(). Because
    // FuncStat.FunctionTotalSampleMap is sorted in descending order of total
    // samples, once we see a cold function, we know all functions after it are
    // also cold.
    if (!Func.IsHot)
      break;

    ColumnOffset = 0;
    double FuncSamplePercent = static_cast<double>(FuncSample.first) /
                               FuncStat.ProfileTotalSample * 100;
    FOS << FuncSample.first << format(" (%.2f%%)", FuncSamplePercent);
    ColumnOffset += TotalSampleWidth;

    FOS.PadToColumn(ColumnOffset);
    FOS << Func.EntrySample;
    ColumnOffset += EntrySampleWidth;

    FOS.PadToColumn(ColumnOffset);
    FOS << Func.FuncSize;
    ColumnOffset += FuncSizeWidth;

    FOS.PadToColumn(ColumnOffset);
    if (ShowDensity) {
      assert(Func.FuncDensity > 0.0 &&
             "Sample density of hot functions should be non-zero");
      FOS << format("%.2f", Func.FuncDensity);
      ColumnOffset += DensityWidth;
    }

    FOS.PadToColumn(ColumnOffset);
    FOS << FuncSample.second << "\n";
  }
}

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

  FuncStatMap FuncStat(symbol_map);
  if (FuncStat.ProfileTotalSample != 0) {
    raw_os_ostream OS(std::cout);
    if (HotFunctionCutoff <= 0 || HotFunctionCutoff > 100) {
      OS << "--hot-function-cutoff needs to be within (0, 100]. User specified "
            "value is ignored and default value "
         << format("%.2f", DefaultHotFunctionCutoff) << " is used.\n";
      HotFunctionCutoff = DefaultHotFunctionCutoff;
    }
    if (HotFunctionDensityThreshold <= 0) {
      OS << "--hot-function-density-threshold needs to be positive. User "
            "specified value is ignored and default value "
         << format("%.2f", DefaultHotFunctionDensityThreshold) << " is used.\n";
      HotFunctionDensityThreshold = DefaultHotFunctionDensityThreshold;
    }

    double Density = calculateHotFunctionStatAndDensity(FuncStat);
    printDensitySuggestion(Density, OS);
    if (ListHotFunction)
      printHotFunctionList(FuncStat, OS);
  } else {
    llvm::errs() << "This profile is empty. Please check your input for "
                    "hotness and density analysis.\n";
  }

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
