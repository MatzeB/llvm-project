// facebook T96340746
//===- RelocationDiagramTest.cpp - Unit tests for RelocationDiagram ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../../ELF/RelocationDiagram.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace lld::elf;

namespace {

class RelocationDiagramTest : public ::testing::Test {
protected:
  void SetUp() override {}

  std::string generateToString(const RelocationDiagram &diagram,
                               uint64_t srcAddr, uint64_t targetAddr,
                               llvm::StringRef srcSectionName,
                               llvm::StringRef targetSectionName,
                               int64_t addend = 0) {
    std::string result;
    llvm::raw_string_ostream os(result);
    diagram.generate(os, srcAddr, targetAddr, srcSectionName, targetSectionName,
                     addend);
    return result;
  }
};

// Test formatSize helper function
TEST_F(RelocationDiagramTest, FormatSizeBytes) {
  EXPECT_EQ("0 B", RelocationDiagram::formatSize(0));
  EXPECT_EQ("1 B", RelocationDiagram::formatSize(1));
  EXPECT_EQ("512 B", RelocationDiagram::formatSize(512));
  EXPECT_EQ("1023 B", RelocationDiagram::formatSize(1023));
}

TEST_F(RelocationDiagramTest, FormatSizeKB) {
  EXPECT_EQ("1.00 KiB", RelocationDiagram::formatSize(1024));
  EXPECT_EQ("1.50 KiB", RelocationDiagram::formatSize(1536));
  EXPECT_EQ("100.00 KiB", RelocationDiagram::formatSize(102400));
}

TEST_F(RelocationDiagramTest, FormatSizeMB) {
  EXPECT_EQ("1.00 MiB", RelocationDiagram::formatSize(1048576));
  EXPECT_EQ("55.79 MiB", RelocationDiagram::formatSize(58501336));
}

TEST_F(RelocationDiagramTest, FormatSizeGB) {
  EXPECT_EQ("1.00 GiB", RelocationDiagram::formatSize(1073741824));
  EXPECT_EQ("1.09 GiB", RelocationDiagram::formatSize(1168264364));
}

// Test empty diagram when no sections
TEST_F(RelocationDiagramTest, EmptyDiagramNoSections) {
  RelocationDiagram diagram;
  std::string result =
      generateToString(diagram, 0x4AD39287, 0x0D5A3FC0, ".text", ".rodata");
  EXPECT_TRUE(result.empty());
}

// Test empty diagram when sections have addr=0
TEST_F(RelocationDiagramTest, EmptyDiagramZeroAddrSections) {
  RelocationDiagram diagram;
  diagram.addSection(".debug_info", 0, 100000);
  diagram.addSection(".debug_line", 0, 200000);
  std::string result =
      generateToString(diagram, 0x4AD39287, 0x0D5A3FC0, ".text", ".rodata");
  EXPECT_TRUE(result.empty());
}

// Test basic diagram generation with two sections
TEST_F(RelocationDiagramTest, BasicDiagramGeneration) {
  RelocationDiagram diagram;
  diagram.addSection(".rodata", 0x0AE4B8C0, 58501336);
  diagram.addSection(".text", 0x0E667B00, 1168264364);

  std::string result =
      generateToString(diagram, 0x4AD39287, 0x0D5A3FC0, ".text", ".rodata");

  EXPECT_FALSE(result.empty());
  EXPECT_NE(std::string::npos, result.find("Memory layout"));
  EXPECT_NE(std::string::npos, result.find(".rodata"));
  EXPECT_NE(std::string::npos, result.find(".text"));
  EXPECT_NE(std::string::npos, result.find("TARGET"));
  EXPECT_NE(std::string::npos, result.find("SOURCE"));
  EXPECT_NE(std::string::npos, result.find("Distance:"));
}

// Test that debug sections (addr=0) are filtered out
TEST_F(RelocationDiagramTest, DebugSectionsFiltered) {
  RelocationDiagram diagram;
  diagram.addSection(".debug_info", 0, 568940000);
  diagram.addSection(".debug_line", 0, 1550000000);
  diagram.addSection(".rodata", 0x0AE4B8C0, 58501336);
  diagram.addSection(".text", 0x0E667B00, 1168264364);

  std::string result =
      generateToString(diagram, 0x4AD39287, 0x0D5A3FC0, ".text", ".rodata");

  EXPECT_FALSE(result.empty());
  EXPECT_EQ(std::string::npos, result.find(".debug_info"));
  EXPECT_EQ(std::string::npos, result.find(".debug_line"));
}

// Test when target is at lower address than source
TEST_F(RelocationDiagramTest, TargetLowerThanSource) {
  RelocationDiagram diagram;
  diagram.addSection(".rodata", 0x0AE4B8C0, 58501336);
  diagram.addSection(".text", 0x0E667B00, 1168264364);

  std::string result =
      generateToString(diagram, 0x4AD39287, 0x0D5A3FC0, ".text", ".rodata");

  size_t targetPos = result.find("TARGET");
  size_t sourcePos = result.find("SOURCE");
  EXPECT_NE(std::string::npos, targetPos);
  EXPECT_NE(std::string::npos, sourcePos);
  EXPECT_LT(targetPos, sourcePos);
}

// Test when source is at lower address than target
TEST_F(RelocationDiagramTest, SourceLowerThanTarget) {
  RelocationDiagram diagram;
  diagram.addSection(".text", 0x0AE4B8C0, 1168264364);
  diagram.addSection(".rodata", 0x50000000, 58501336);

  std::string result =
      generateToString(diagram, 0x0D5A3FC0, 0x52000000, ".text", ".rodata");

  size_t targetPos = result.find("TARGET");
  size_t sourcePos = result.find("SOURCE");
  EXPECT_NE(std::string::npos, targetPos);
  EXPECT_NE(std::string::npos, sourcePos);
  EXPECT_LT(sourcePos, targetPos);
}

// Test arm connectors appear and are aligned
TEST_F(RelocationDiagramTest, ArmConnectorsPresent) {
  RelocationDiagram diagram;
  diagram.addSection(".rodata", 0x0AE4B8C0, 58501336);
  diagram.addSection(".text", 0x0E667B00, 1168264364);

  std::string result =
      generateToString(diagram, 0x4AD39287, 0x0D5A3FC0, ".text", ".rodata");

  EXPECT_NE(std::string::npos, result.find("<--+"));
}

// Test with fixed expected output - simple two-section layout with gap
TEST_F(RelocationDiagramTest, FixedOutputFormat) {
  RelocationDiagram diagram;
  diagram.addSection(".rodata", 0x1000, 0x1000);
  diagram.addSection(".text", 0x3000, 0x2000);

  std::string result =
      generateToString(diagram, 0x4000, 0x1500, ".text", ".rodata", 0);

  std::string expected = R"(

Memory layout:

    0x00001000 - .rodata (4.00 KiB)
    0x00001500 - >>> TARGET          <--+
    0x00002000 - [gap] (4.00 KiB)       |
    0x00003000 - .text (8.00 KiB)       |
    0x00004000 - >>> SOURCE          ---+

    Distance: -11008 bytes (10.75 KiB)

)";

  EXPECT_EQ(expected, result) << "Expected:\n"
                              << expected << "\nActual:\n"
                              << result;
}

// Test with intermediate sections (contiguous, no gaps >= 4KB)
TEST_F(RelocationDiagramTest, IntermediateSections) {
  RelocationDiagram diagram;
  diagram.addSection(".rodata", 0x1000, 0x1000);
  diagram.addSection(".data", 0x2000, 0x800);
  diagram.addSection(".bss", 0x2800, 0x800);
  diagram.addSection(".text", 0x3000, 0x2000);

  std::string result =
      generateToString(diagram, 0x4000, 0x1500, ".text", ".rodata", 0);

  std::string expected = R"(

Memory layout:

    0x00001000 - .rodata (4.00 KiB)
    0x00001500 - >>> TARGET          <--+
    0x00002000 - .data (2.00 KiB)       |
    0x00002800 - .bss (2.00 KiB)        |
    0x00003000 - .text (8.00 KiB)       |
    0x00004000 - >>> SOURCE          ---+

    Distance: -11008 bytes (10.75 KiB)

)";

  EXPECT_EQ(expected, result) << "Expected:\n"
                              << expected << "\nActual:\n"
                              << result;
}

// Test with realistic MKL
TEST_F(RelocationDiagramTest, RealisticMKLOverflowScenario) {
  RelocationDiagram diagram;
  diagram.addSection(".rodata", 0x0AE4B8C0, 58501336);
  diagram.addSection("fb_build_info", 0x0E6161A0, 883);
  diagram.addSection("protodesc_cold", 0x0E616520, 329096);
  diagram.addSection(".text", 0x0E667B00, 1168264364);

  std::string result = generateToString(diagram, 0x4AD39287, 0x0D5A3FC0,
                                        ".text", ".rodata", -1140850688);

  // Should show the intermediate sections
  EXPECT_NE(std::string::npos, result.find("fb_build_info"));
  EXPECT_NE(std::string::npos, result.find("protodesc_cold"));

  // Should show the arm connecting through all sections
  EXPECT_NE(std::string::npos, result.find("|"));
}

} // namespace
