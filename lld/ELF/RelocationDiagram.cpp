// facebook T96340746
//===- RelocationDiagram.cpp - Visualize relocation overflows ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RelocationDiagram.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include <algorithm>
#include <cmath>

using namespace llvm;

namespace lld::elf {

/// Calculate the number of hex digits needed to display an address.
/// Returns at least 8 digits (for 32-bit addresses), adding more for
/// addresses that exceed 32 bits (e.g., 9 digits for 0x100000000).
static int hexDigitsForAddress(uint64_t addr) {
  int width = 8;
  uint64_t upperBits = addr >> 32;
  while (upperBits > 0) {
    width++;
    upperBits >>= 4;
  }
  return width;
}

RelocationDiagram::RelocationDiagram(SmallVector<DiagramSection, 16> secs)
    : sections(std::move(secs)) {}

void RelocationDiagram::addSection(StringRef name, uint64_t addr,
                                   uint64_t size) {
  sections.emplace_back(name, addr, size);
}

std::string RelocationDiagram::formatSize(uint64_t bytes) {
  const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int unitIdx = 0;
  double size = static_cast<double>(bytes);
  while (size >= 1024 && unitIdx < 4) {
    size /= 1024;
    unitIdx++;
  }
  if (unitIdx == 0)
    return (Twine(bytes) + " " + units[unitIdx]).str();
  return formatv("{0:F2} {1}", size, units[unitIdx]);
}

void RelocationDiagram::generate(raw_ostream &os, uint64_t srcAddr,
                                 uint64_t targetAddr, StringRef srcSectionName,
                                 StringRef targetSectionName,
                                 int64_t addend) const {
  // Find the source and target sections
  const DiagramSection *srcSec = nullptr;
  const DiagramSection *targetSec = nullptr;

  for (const auto &sec : sections) {
    if (sec.size == 0 || sec.addr == 0)
      continue;
    uint64_t secEnd = sec.end();
    if (sec.name == srcSectionName && sec.addr <= srcAddr && secEnd > srcAddr)
      srcSec = &sec;
    if (sec.name == targetSectionName && sec.addr <= targetAddr &&
        secEnd > targetAddr)
      targetSec = &sec;
  }

  if (!srcSec || !targetSec)
    return;

  // Collect only sections between source and target (inclusive)
  uint64_t minAddr = std::min(srcSec->addr, targetSec->addr);
  uint64_t maxAddr = std::max(srcSec->end(), targetSec->end());

  auto isAllocated = [](const DiagramSection &sec) {
    return sec.size != 0 && sec.addr != 0;
  };
  auto overlapsRange = [&](const DiagramSection &sec) {
    return sec.end() > minAddr && sec.addr < maxAddr;
  };

  SmallVector<DiagramSection, 16> relevantSections;
  llvm::copy_if(sections, std::back_inserter(relevantSections),
                [&](const DiagramSection &sec) {
                  return isAllocated(sec) && overlapsRange(sec);
                });

  llvm::sort(relevantSections,
             [](const DiagramSection &a, const DiagramSection &b) {
               return a.addr < b.addr;
             });

  if (relevantSections.empty())
    return;

  int64_t displacement =
      static_cast<int64_t>(targetAddr) + addend - static_cast<int64_t>(srcAddr);

  os << "\n\nMemory layout:\n\n";

  struct Entry {
    uint64_t addr;
    std::string label;
    bool isSource;
    bool isTarget;
    bool isGap;
  };
  SmallVector<Entry, 32> entries;

  constexpr uint64_t kMinGapSize = 4096;
  for (size_t i = 0; i < relevantSections.size(); ++i) {
    const auto &sec = relevantSections[i];
    entries.push_back({sec.addr, sec.name + " (" + formatSize(sec.size) + ")",
                       false, false, false});

    if (i + 1 < relevantSections.size()) {
      uint64_t gapStart = sec.end();
      uint64_t gapEnd = relevantSections[i + 1].addr;
      if (gapEnd > gapStart) {
        uint64_t gapSize = gapEnd - gapStart;
        if (gapSize >= kMinGapSize) {
          entries.push_back({gapStart, "[gap] (" + formatSize(gapSize) + ")",
                             false, false, true});
        }
      }
    }
  }

  entries.push_back({srcAddr, ">>> SOURCE", true, false, false});
  entries.push_back({targetAddr, ">>> TARGET", false, true, false});

  llvm::sort(entries,
             [](const Entry &a, const Entry &b) { return a.addr < b.addr; });

  size_t srcIdx = 0, targetIdx = 0;
  size_t maxLabelLen = 0;
  uint64_t maxAddrValue = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].isSource)
      srcIdx = i;
    if (entries[i].isTarget)
      targetIdx = i;
    if (entries[i].label.size() > maxLabelLen)
      maxLabelLen = entries[i].label.size();
    if (entries[i].addr > maxAddrValue)
      maxAddrValue = entries[i].addr;
  }
  size_t firstIdx = std::min(srcIdx, targetIdx);
  size_t lastIdx = std::max(srcIdx, targetIdx);

  int addrWidth = hexDigitsForAddress(maxAddrValue);

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &e = entries[i];
    os << "    0x" << format_hex_no_prefix(e.addr, addrWidth) << " - "
       << e.label;

    size_t padding = maxLabelLen - e.label.size();
    os.indent(padding);

    if (e.isSource)
      os << "  ---+";
    else if (e.isTarget)
      os << "  <--+";
    else if (i > firstIdx && i < lastIdx)
      os << "     |";

    os << "\n";
  }

  os << "\n";
  os << "    Distance: " << displacement << " bytes ("
     << formatSize(std::abs(displacement)) << ")\n\n";
}

} // namespace lld::elf
