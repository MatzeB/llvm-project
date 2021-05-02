//===-- PerfSampleReader.h - Instruction class definition -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of a parser for
/// perf script output
/// facebook T26943842
//===----------------------------------------------------------------------===//

#pragma once

#include "assert.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "SampleReader.h"
#include "SymbolLoader.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Regex.h"
#include <numeric>

#define DEBUG_TYPE "autofdo_perf_parser"
extern llvm::cl::opt<std::string> PrintPerformanceSamples;
extern llvm::cl::opt<double> MinEventLineNumber;
extern llvm::cl::opt<double> MaxEventLineNumber;

namespace autofdo {
using namespace llvm;

struct MemoryMapping {
  std::string ObjectFile;
  uint64_t Length;
  uint64_t StartAddress;
  uint64_t Offset;
  uint64_t PreferedLoadAddress;
  // TODO: use std::tie or something
  static bool intersects(const MemoryMapping &a, const MemoryMapping &b) {
    auto &lowest_mapping = a.StartAddress < b.StartAddress ? a : b;
    auto &highest_mapping = a.StartAddress < b.StartAddress ? b : a;
    return (lowest_mapping.StartAddress + lowest_mapping.Length) >=
           highest_mapping.StartAddress;
  }

  bool operator<(const MemoryMapping &rhs) const {
    return StartAddress < rhs.StartAddress;
  }

  bool operator>(const MemoryMapping &rhs) const { return rhs < *this; }

  bool operator<=(const MemoryMapping &rhs) const { return !(rhs < *this); }

  bool operator>=(const MemoryMapping &rhs) const { return !(*this < rhs); }

  friend std::ostream &operator<<(std::ostream &os,
                                  const MemoryMapping &mapping) {
    os << "ObjectFile: " << mapping.ObjectFile << " length: " << mapping.Length
       << " startAddress: " << mapping.StartAddress
       << " offset: " << mapping.Offset;
    return os;
  }
};

struct lbrElement {
  friend std::ostream &operator<<(std::ostream &os, const lbrElement &element) {
    os << "to: " << element.To << " from: " << element.From;
    return os;
  }
  uint64_t From;
  uint64_t To;
};

// TODO: use a templated size with std::array<32> to avoid allocations on
// std::vector;
struct perfEvent {
  uint64_t InstructionPointer;
  std::vector<lbrElement> BranchStack;
};

/*
 * load samples from perf script --no-demangle --show-mmap-events -F ip,brstack
 * output
 * TODO: use string_view vs string for more efficient parsing
 * TODO: add support for unmapping events
 * TODO: we generally assume that the event period is constant, this might not
 * be the case
 * TODO: switch to named groups in the regexes
 * */

class PerfDataSampleReader : public AbstractSampleReader {

  std::vector<Range> ranges;
  std::set<MemoryMapping> mappedAddressSpace;
  std::string profile;
  // maps the filename to the full path given as --binary arguments
  std::map<std::string, std::string> fileNames;
  std::map<Branch, uint64_t> branchCountMap;
  std::map<Range, uint64_t> rangeCountMap;
  std::map<InstructionLocation, uint64_t> ip_count_map;
  uint64_t nbDropedBranch;
  uint64_t nbDropedIP;
  uint64_t nbDropedRange;
  std::ostream &log;
  uint64_t processedEvent;

  // PERF_RECORD_MMAP2 16781/16781: [0x563f9f223000(0x237000) @ 0 08:06 38535520
  // 522742584]: r-xp /usr/bin/find
  Regex reg_mmap2;
  Regex reg_map;
  Regex lbr_reg;
  Regex reg_event;

  static const int maxRangeLenth = 1048576;

public:
  PerfDataSampleReader(const std::string &profile,
                       const std::vector<std::string> &paths)
      : profile(profile), nbDropedBranch(0), nbDropedIP(0), nbDropedRange(0),
        log(std::cout), processedEvent(0),

#define HEXSET "[a-f0-9]"
#define HEXCHAR "[a-f0-9]+"
#define HEXLIT "0x" HEXCHAR

#define MAP_REGEX                                                              \
  "\\[(" HEXLIT ")\\((" HEXLIT ")\\) @ (" HEXLIT "|0)( .*)*\\]: [-a-z]+ (.*)"
        //       matches        1=addr         2=len              3=pgoff 4=junk
        //       5=filename
        reg_mmap2("PERF_RECORD_MMAP2 .*: " MAP_REGEX),
        reg_map("PERF_RECORD_MMAP .*: " MAP_REGEX),

#define LBR_REG "(" HEXLIT ")/(" HEXLIT ")/[P M]/[P \\-]/[P \\-]/" HEXCHAR
        //       matches    1=from       2=to
        lbr_reg(LBR_REG), reg_event("[ \t]+(" HEXSET "*)(( " LBR_REG " )+)") {

    for (const auto &path : paths)
      fileNames[std::string(llvm::sys::path::filename(path))] = path;
  };

private:
  /// Dump the content after parsing into a  text stream
  void Dump(std::ostream &ss) {
    // Sort the sample perf ObjectFile

    std::set<std::string> objectFiles;
    std::map<std::string, std::map<Branch, uint64_t>> branchCountPerFile;
    for (const auto &branchCount : branchCountMap) {
      branchCountPerFile[branchCount.first.instruction.objectFile]
                        [branchCount.first] = branchCount.second;
      objectFiles.insert(branchCount.first.instruction.objectFile);
    }

    std::map<std::string, std::map<Range, uint64_t>> rangeCountMapPerFile;
    for (const auto &rangeCount : rangeCountMap) {
      rangeCountMapPerFile[rangeCount.first.begin.objectFile]
                          [rangeCount.first] = rangeCount.second;
      objectFiles.insert(rangeCount.first.begin.objectFile);
    }

    std::map<std::string, std::map<InstructionLocation, uint64_t>>
        ip_count_map_per_file;
    for (const auto &IpCount : ip_count_map) {
      ip_count_map_per_file[IpCount.first.objectFile][IpCount.first] =
          IpCount.second;
      objectFiles.insert(IpCount.first.objectFile);
    }

    for (const auto &objectFile : objectFiles) {
      ss << "Samples for " << objectFile << std::endl;
      ss << rangeCountMapPerFile[objectFile].size() << std::endl;
      for (const auto &rangeCount : rangeCountMapPerFile[objectFile]) {
        ss << std::hex
           << rangeCount.first.begin.offset -
                  SymbolLoader::preferedBasedAddress(objectFile)
           << std::dec << "-" << std::hex
           << rangeCount.first.end.offset - SymbolLoader::preferedBasedAddress(
                                                rangeCount.first.end.objectFile)
           << std::dec << ":" << rangeCount.second << std::endl;
      }
      ss << ip_count_map_per_file[objectFile].size() << std::endl;
      for (const auto &IpCount : ip_count_map_per_file[objectFile]) {
        ss << std::hex
           << IpCount.first.offset -
                  SymbolLoader::preferedBasedAddress(objectFile)
           << std::dec << ":" << IpCount.second << std::endl;
      }

      ss << branchCountPerFile[objectFile].size() << std::endl;
      for (const auto &branchCount : branchCountPerFile[objectFile]) {
        ss << std::hex
           << branchCount.first.instruction.offset -
                  SymbolLoader::preferedBasedAddress(objectFile)
           << std::dec << "->" << std::hex
           << branchCount.first.target.offset -
                  SymbolLoader::preferedBasedAddress(
                      branchCount.first.target.objectFile)
           << std::dec << ":" << branchCount.second << std::endl;
      }
    }
  }

  /// Translate 'real  mapped virtual address into the "prefered addresse space
  /// to of the map binary In practice, for executable the vitual address is
  /// unchanged, for dynamic object, the virtual address is translated as if the
  /// .so was loaded at address zero. This translation relies on the two
  /// properties : 1 - The virtual address is in the first loadble segments 2 -
  /// The first loadble segment as a file offset of  zero \param virtual address
  /// to resolve \return InstructionLocation corresponding to the address or
  /// error if the address
  ///         is not mapped in the process memory space
  Optional<InstructionLocation> resolveAddress(uint64_t address) {
    auto mapping = std::lower_bound(
        mappedAddressSpace.begin(), mappedAddressSpace.end(), address,
        [](const MemoryMapping &mapped, uint64_t addr) {
          return mapped.StartAddress < addr;
        });

    // if mapping == begin, means that address is lower than the lowest start
    // address of any mapped region.

    if (mapping == mappedAddressSpace.begin()) {
      LLVM_DEBUG(log << "absolute address : " << std::hex << address << std::dec
                     << ", could not be resolved " << std::endl);
      return Optional<InstructionLocation>{};
    }
    mapping--;
    if (address > ((mapping)->StartAddress + (mapping)->Length)) {
      LLVM_DEBUG(log << "absolute address : " << std::hex << address << std::dec
                     << ", could not be resolved " << std::endl);
      return Optional<InstructionLocation>{};
    }

    {
      auto offset = (address - mapping->StartAddress) + mapping->PreferedLoadAddress;
      InstructionLocation ret{mapping->ObjectFile, offset};
      LLVM_DEBUG(log << "absolute address : " << std::hex << address << std::dec
                     << ", resolved to " << ret << std::endl);
      LLVM_DEBUG(log << "absolute address : " << std::hex << address << std::dec
                     << ", resolved to " << ret << std::endl);
      return ret;
    };
  };

  static uint64_t stoull(StringRef S) {
    uint64_t value = 0;
    if (S.substr(0, 2) == "0x")
      S = S.substr(2);
    // TODO -- what happens when this fails
    S.getAsInteger(16, value);
    return value;
  }

  Optional<MemoryMapping> parseMMAP2(const std::string &line) {
    SmallVector<StringRef, 5> results;
    bool matched = reg_mmap2.match(line, &results);
#ifndef NDEBUG
    bool shouldMatch = StringRef(line).startswith("PERF_RECORD_MMAP2 ");
    assert(matched == shouldMatch);
#endif
    if (!matched)
      return Optional<MemoryMapping>{};

    StringRef objectFile(results[5]);
    auto filename =
        fileNames.find(std::string(llvm::sys::path::filename(objectFile)));
    if (filename == fileNames.end())
      return Optional<MemoryMapping>{};
    uint64_t base = SymbolLoader::preferedBasedTextAddress(filename->second);
    uint64_t loadAddress = stoull(results[1]);
    uint64_t length = stoull(results[2]);
    uint64_t offset = stoull(results[3]);

    return MemoryMapping{filename->second, length, loadAddress, offset, base};
  };

  Optional<MemoryMapping> parseMMAP(const std::string &line) {
    SmallVector<StringRef, 5> results;
    bool matched = reg_map.match(line, &results);
#ifndef NDEBUG
    bool shouldMatch = StringRef(line).startswith("PERF_RECORD_MMAP ");
    assert(matched == shouldMatch);
#endif
    if (!matched)
      return Optional<MemoryMapping>{};
    StringRef objectFile(results[5]);
    auto filename =
        fileNames.find(std::string(llvm::sys::path::filename(objectFile)));
    if (filename == fileNames.end())
      // todo -- terminate processing, no other thing will match
      return Optional<MemoryMapping>{};

    uint64_t loadAddress = stoull(results[1]);
    uint64_t length = stoull(results[2]);
    uint64_t offset = stoull(results[3]);
    uint64_t PreferedLoadAddr = 0;
    return MemoryMapping{std::string(objectFile), length, loadAddress, offset,
                         PreferedLoadAddr};
  };

  Optional<perfEvent> parsePerfEvent(const std::string &line) {
    SmallVector<StringRef, 5> results;
    bool matched = reg_event.match(line, &results);
    if (!matched)
      return Optional<perfEvent>{};
    perfEvent ret;
    ret.InstructionPointer = stoull(results[1]);
    StringRef LBRs = results[2].substr(1);
    while (LBRs.size() > 0) {
      size_t Next = LBRs.find("  ");
      StringRef cur = LBRs.substr(0, Next);
      matched = lbr_reg.match(cur, &results);
      assert(matched && "failed to match LBR regex");
      uint64_t from = stoull(results[1]);
      uint64_t to = stoull(results[2]);
      ret.BranchStack.emplace_back(lbrElement{from, to});
      if (Next == StringRef::npos)
        break;
      LBRs = LBRs.substr(Next + 2);
    }
    return ret;
  };

  // Does the given mapping conflicts with the current address space ;
  bool conflictingMemoryMapping(const MemoryMapping &memoryMap) {

    // TODO: use find_first of for abetter error message
    auto conflict =
        std::any_of(mappedAddressSpace.begin(), mappedAddressSpace.end(),
                    [&memoryMap](const MemoryMapping &mapping) {
                      return MemoryMapping::intersects(mapping, memoryMap);
                    });
    if (conflict) {
      log << "New mapping is conflicting with existing mapping " << std::endl;
      return true;
    }
    return false;
  };

  // Debug functions
  void printAddressSpace() {
    log << "Address space : " << std::endl;
    for (const auto &e : mappedAddressSpace) {
      log << e << std::endl;
    }
  }

  void parseSingleLine(const std::string &line) {
    LLVM_DEBUG(log << "parsing line : " << line << std::endl);
    if (auto memoryMap = parseMMAP2(line)) {
      mappedAddressSpace.insert(memoryMap.getValue());
    } else if (auto memoryMap = parseMMAP(line)) {
      mappedAddressSpace.insert(memoryMap.getValue());
    } else if (auto event = parsePerfEvent(line)) {
      processedEvent++;

      if (MinEventLineNumber.getNumOccurrences() > 0) {
        if (processedEvent < MinEventLineNumber.getValue()) {
          std::cerr << "skip event " << processedEvent
                    << " smaller than min event" << std::endl;
          return;
        }
      }

      if (MaxEventLineNumber.getNumOccurrences() > 0) {
        if (processedEvent > MaxEventLineNumber.getValue()) {
          std::cerr << "skip event " << processedEvent
                    << " higher than max event" << std::endl;
          return;
        }
      }

      if (event.getValue().InstructionPointer) {
        if (auto resolved_ip =
                resolveAddress(event.getValue().InstructionPointer)) {
          ip_count_map[resolved_ip.getValue()] += 1;
        } else {
          nbDropedIP += 1;
          log << "on event : " << line << std::endl;
          log << "dropping address count because could not resolve : "
              << std::hex << event.getValue().InstructionPointer << std::dec
              << std::endl;
        }
      }

      std::reverse(event.getValue().BranchStack.begin(),
                   event.getValue().BranchStack.end());
      std::vector<std::pair<Optional<InstructionLocation>,
                            Optional<InstructionLocation>>>
          lbr;
      for (const auto &e : event.getValue().BranchStack) {
        auto from = resolveAddress(e.From);
        auto to = resolveAddress(e.To);
        lbr.push_back(std::make_pair(from, to));
      }

      size_t i = 0;
      for (; i < lbr.size() - 1; i++) {
        LLVM_DEBUG(log << "LBR element : " << i << std::endl);
        LLVM_DEBUG(log << std::hex << event.getValue().BranchStack[i] << std::dec << std::endl);
        auto &from = lbr[i].first;
        auto &to = lbr[i].second;

        if ((to) and (from)) {
          Branch br{from.getValue(), to.getValue()};
          branchCountMap[br] += 1;
          LLVM_DEBUG(log << "New LBR element : " << br << std::endl);
        } else {
          nbDropedBranch += 1;
          LLVM_DEBUG(log << "Dropping lbr element : "
                          << std::hex  << event.getValue().BranchStack[i] << std::dec <<  std::endl);
        }

        auto &next_from = lbr[i + 1].first;
        if (to and next_from) {
          if ((event.getValue().BranchStack[i].To <=
               event.getValue().BranchStack[i + 1].From) and
              (event.getValue().BranchStack[i + 1].From -
                   event.getValue().BranchStack[i].To <
               maxRangeLenth)) {
            if (to.getValue().objectFile == next_from.getValue().objectFile) {
              InstructionLocation from{next_from.getValue().objectFile, next_from.getValue().offset+1};
              Range range{to.getValue(), from};
              rangeCountMap[range] += 1;
              LLVM_DEBUG(log << "New Range : " << std::hex << range << std::dec
                             << std::endl);
            } else {
              log << "dropping Range : " << std::hex
                  << Range{to.getValue(), next_from.getValue()} << std::dec
                  << " Miss match objetFiles" << std::endl;
            }
          } else {
            LLVM_DEBUG(log << std::hex << "Dropping invalid range : begin = "
                           << event.getValue().BranchStack[i].To << ", end = "
                           << event.getValue().BranchStack[i + 1].From
                           << std::dec << std::endl);
            nbDropedRange += 1;
          }
        }
      }

      // last lb element
      assert(i == lbr.size() - 1);
      auto &from = lbr[i].first;
      auto &to = lbr[i].second;

      if ((to) and (from)) {
        Branch br{from.getValue(), to.getValue()};
        branchCountMap[br] += 1;
        LLVM_DEBUG(log << "New LBR element : " << std::hex << br << std::dec
                       << std::endl);
      } else {
        LLVM_DEBUG(log << "Dropping lbr element : "
                       << std::hex << event.getValue().BranchStack[i] <<  std::dec << std::endl);
      }

    } else {
      LLVM_DEBUG(log << "parsing failed " << std::endl);
    }
  }

  const AddressCountMap &address_count_map() const override {
    return ip_count_map;
  }

  const RangeCountMap &range_count_map() const override {
    return rangeCountMap;
  }

  const BranchCountMap &branch_count_map() const override {
    return branchCountMap;
  }

  uint64_t GetTotalSampleCount() const override { return 0; }

  uint64_t GetTotalCount() const override { return 0; }

public:
  bool readProfile() override {
    std::ifstream input(profile);
    if (input.fail()) {
      std::cerr << profile << " failed" << std::endl;
      exit(-1);
    }
    std::string line;
    int count = 0;
    for (; !input.eof(); count++) {
      std::getline(input, line);
      parseSingleLine(line);
    };

    auto total_valid_ip =
        std::accumulate(ip_count_map.begin(), ip_count_map.end(), 0,
                        [](int acc, decltype(*ip_count_map.begin()) b) {
                          return acc + b.second;
                        });
    auto total_valid_branches =
        std::accumulate(branchCountMap.begin(), branchCountMap.end(), 0,
                        [](int acc, decltype(*branchCountMap.begin()) b) {
                          return acc + b.second;
                        });
    auto total_valid_ranges =
        std::accumulate(rangeCountMap.begin(), rangeCountMap.end(), 0,
                        [](int acc, decltype(*rangeCountMap.begin()) b) {
                          return acc + b.second;
                        });

    log << "Total number of Address parsed : " << total_valid_ip + nbDropedIP
        << " dropped " << nbDropedIP << std::endl;

    log << "Total number of branch parsed : "
        << total_valid_branches + nbDropedBranch << " dropped "
        << nbDropedBranch << std::endl;

    log << "Total number of range parsed : "
        << total_valid_ranges + nbDropedRange << " dropped " << nbDropedRange
        << std::endl;

    if (PrintPerformanceSamples.getNumOccurrences() > 0) {
      std::ofstream samplesFile(PrintPerformanceSamples);
      Dump(samplesFile);
      samplesFile.close();
    }
    return true;
  };
};
} // namespace autofdo
#undef DEBUG_TYPE
