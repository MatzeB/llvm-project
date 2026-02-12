// facebook T96340746
//===- RelocationDiagram.h - Visualize relocation overflows ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides utilities to generate ASCII diagrams for visualizing
// relocation overflow errors. This helps developers understand memory layout
// issues when relocations exceed their addressable range.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_RELOCATION_DIAGRAM_H
#define LLD_ELF_RELOCATION_DIAGRAM_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>

namespace lld::elf {

/// Represents a memory section for diagram generation.
/// This is a simplified view of an output section, containing only
/// the information needed for visualization.
struct DiagramSection {
  std::string name;
  uint64_t addr;
  uint64_t size;

  uint64_t end() const { return addr + size; }

  DiagramSection(llvm::StringRef n, uint64_t a, uint64_t s)
      : name(n.str()), addr(a), size(s) {}
};

/// Generates ASCII diagrams showing memory layout for relocation overflows.
///
/// Example output:
/// ```
/// Memory layout:
///
///     0x0AE4B8C0 - .rodata (55.79 MB)
///     0x0D5A3FC0 - >>> TARGET            <--+
///     0x0E6161A0 - fb_build_info (883 B)    |
///     0x0E616520 - protodesc_cold (321 KB)  |
///     0x0E667B00 - .text (1.09 GB)          |
///     0x4AD39287 - >>> SOURCE            <--+
///
///     Distance: -1031361223 bytes (983.58 MB)
/// ```
class RelocationDiagram {
public:
  /// Construct a diagram generator with the given sections.
  /// @param sections All output sections in the binary (will be filtered).
  explicit RelocationDiagram(
      llvm::SmallVector<DiagramSection, 16> sections = {});

  /// Add a section to the diagram.
  void addSection(llvm::StringRef name, uint64_t addr, uint64_t size);

  /// Generate the ASCII diagram showing the relocation from source to target.
  /// Output is written directly to the provided stream.
  /// @param os Output stream to write the diagram to
  /// @param srcAddr Address of the source instruction
  /// @param targetAddr Address of the target symbol
  /// @param srcSectionName Name of the section containing the source
  /// @param targetSectionName Name of the section containing the target
  /// @param addend The relocation addend (affects final displacement)
  void generate(llvm::raw_ostream &os, uint64_t srcAddr, uint64_t targetAddr,
                llvm::StringRef srcSectionName,
                llvm::StringRef targetSectionName, int64_t addend = 0) const;

  /// Format a size in human-readable form (e.g., "1.23 GB").
  static std::string formatSize(uint64_t bytes);

private:
  llvm::SmallVector<DiagramSection, 16> sections;
};

} // namespace lld::elf

#endif // LLD_ELF_RELOCATION_DIAGRAM_H
